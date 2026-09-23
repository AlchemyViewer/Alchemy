/**
 * @file alscriptsnippets_test.cpp
 * @brief The scripter's own snippets, read and written, and a file that does not read kept.
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

#include "../alscriptsnippets.h"

#include "fsyspath.h"
#include "llfile.h"
#include "lluuid.h"

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace tut
{
    struct alscriptsnippets_data
    {
        std::string folder;

        alscriptsnippets_data()
        {
            folder = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alscriptsnippets_" + LLUUID::generateNewID().asString())).string();
            std::error_code ec;
            std::filesystem::create_directories(fsyspath(folder), ec);
        }
        ~alscriptsnippets_data()
        {
            std::error_code ec;
            std::filesystem::remove_all(fsyspath(folder), ec);
        }

        static std::string read(const std::string& file)
        {
            std::ifstream     in(fsyspath(file), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }

        static ALScriptSnippets::Snippet snippet(const char* name, const char* body)
        {
            ALScriptSnippets::Snippet one;
            one.name = name;
            one.body = body;
            return one;
        }
    };

    typedef test_group<alscriptsnippets_data> alscriptsnippets_group;
    typedef alscriptsnippets_group::object    alscriptsnippets_object;
    alscriptsnippets_group                    alscriptsnippets_instance("alscriptsnippets");

    template<> template<>
    void alscriptsnippets_object::test<1>()
    {
        set_test_name("written and read back; one being written, with no body yet, stays out of the file");
        const std::string file = folder + "/lsl.xml";
        std::vector<ALScriptSnippets::Snippet> none;
        ensure("no file is no snippets, and nothing wrong", ALScriptSnippets::readFrom(file, false, none) && none.empty());
        ensure("written", ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"$1\");"), snippet("half", "") }));
        std::vector<ALScriptSnippets::Snippet> back;
        ensure("read", ALScriptSnippets::readFrom(file, false, back));
        ensure("the one with a body", back.size() == 1 && back[0].name == "say" && back[0].body == "llSay(0, \"$1\");");
    }

    template<> template<>
    void alscriptsnippets_object::test<2>()
    {
        set_test_name("a file that does not read as snippets is kept beside, not written over");
        const std::string file   = folder + "/lsl.xml";
        const std::string broken = "<llsd><array><map><key>name</key><string>mine</string></map>  <!-- a comma too far -->";
        {
            std::ofstream out(fsyspath(file), std::ios::binary);
            out << broken;
        }
        std::vector<ALScriptSnippets::Snippet> held;
        ensure("it does not read", !ALScriptSnippets::readFrom(file, false, held));
        ensure("written all the same", ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"hi\");") }));
        ensure("what it held kept, as it was", LLFile::isfile(file + ".unreadable") && read(file + ".unreadable") == broken);
        std::vector<ALScriptSnippets::Snippet> back;
        ensure("the new file reads", ALScriptSnippets::readFrom(file, false, back) && back.size() == 1);

        // Broken again: kept beside the first, which is not written over.
        {
            std::ofstream out(fsyspath(file), std::ios::binary);
            out << "again";
        }
        ensure("written", ALScriptSnippets::writeTo(file, { snippet("say", "x") }));
        ensure("the first kept still", read(file + ".unreadable") == broken);
        ensure("the second beside it", read(file + ".unreadable.1") == "again");
    }
}
