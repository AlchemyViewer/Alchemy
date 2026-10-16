/**
 * @file alscriptstudio_stubs.cpp
 * @brief What Script Studio's units reach of the rest of the viewer, stubbed for their tests.
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


#include "../llviewerprecompiledheaders.h"

#include "alscriptstudio_stubs.h"

// The preprocessor's header reaches the inventory model's, which does not
// include what it uses.
#include <boost/unordered_map.hpp>

#include "../alscriptanalysis.h"
#include "../alscriptmodules.h"
#include "../alscriptpreprocessor.h"
#include "../alscripttypes.h"
#include "../lllogchat.h"

// One fake of each, for every test: where two tests want one to behave
// differently, each chooses (alscriptstudio_stubs.h).

// The external editor's log says when on its first line, by the chat log's
// clock, which is the viewer's: a fixed word here, which a test can look
// for.
std::string LLLogChat::timestamp2LogString(U32, bool, bool)
{
    return "[when]";
}

// What the preprocessor calls a script and a file, as it spells them; the
// preprocessor itself is the viewer's.
bool ALScriptPreprocessor::refOf(const std::string& path, ALScriptRef& ref)
{
    if (path.rfind("object:", 0) == 0)
    {
        const size_t colon = path.find(':', 7);
        if (colon == std::string::npos)
        {
            return false;
        }
        ref.object.set(path.substr(7, colon - 7));
        ref.item.set(path.substr(colon + 1));
        return ref.object.notNull() && ref.item.notNull();
    }
    if (path.rfind("inventory:", 0) == 0)
    {
        ref.object.setNull();
        ref.item.set(path.substr(10));
        return ref.item.notNull();
    }
    return false;
}

std::string ALScriptPreprocessor::pathOf(const ALScriptRef& ref)
{
    return ref.inInventory() ? "inventory:" + ref.item.asString() : "object:" + ref.object.asString() + ":" + ref.item.asString();
}

bool ALScriptPreprocessor::fileOf(const std::string& path, std::string& file)
{
    if (path.rfind("disk:", 0) != 0)
    {
        return false;
    }
    file = path.substr(5);
    return !file.empty();
}

// A module's identity is the path it was reached by; the modules index is
// the viewer's.
std::string ALScriptModules::identity(const std::string& path)
{
    return path;
}

// The skin's words for a key are the viewer's: the English they came with.
std::string alScriptKeyedWords(const std::string&, const std::vector<std::string>&, const std::string& english)
{
    return english;
}

// The lints as a scripter chose them are the viewer's settings: what a test
// chose, and an SLua script checked with the defaults.
std::function<void(ALScriptProblems&)>& al_studio_test::chosenLints()
{
    static std::function<void(ALScriptProblems&)> chosen;
    return chosen;
}

void ALScriptLints::apply(ALScriptProblems& problems)
{
    if (const std::function<void(ALScriptProblems&)>& chosen = al_studio_test::chosenLints())
    {
        chosen(problems);
    }
}

ALLuauConfig ALScriptLints::luauBase()
{
    return ALLuauConfig();
}
