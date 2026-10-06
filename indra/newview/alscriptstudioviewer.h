/**
 * @file alscriptstudioviewer.h
 * @brief What Script Studio's units ask of the viewer they run in: one interface, the viewer's or a test's.
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

#include "alluauconfig.h"
#include "alpreprocessor.h"
#include "alscriptmodules.h"
#include "alscriptpreprocessor.h"
#include "llsd.h"

#include <functional>
#include <string>
#include <vector>

// What Script Studio's units ask of the viewer they run in, beyond what
// each is given: the grid's definitions of the languages, the
// preprocessor's words and settings, the preprocessor itself, and the
// modules a script requires. One interface: the viewer attaches its own
// once, at startup (ALScriptStudio::attachViewer), and a test uses one of
// its own while it runs. With none attached, a unit asks a viewer that has
// nothing -- no definitions, no preprocessor -- which is what this class
// answers as it stands.
class ALScriptStudioViewer
{
public:
    virtual ~ALScriptStudioViewer() = default;

    // The one attached, or a viewer with nothing.
    static ALScriptStudioViewer& get();
    // Attached, or none again; the one attached lives while it is.
    static void                  use(ALScriptStudioViewer* viewer);

    // --- the words -------------------------------------------------------

    // The grid's definitions of a language, as the syntax cache gives
    // them; and which definitions they are, so that what is built from
    // them is built again on the first ask after they move.
    virtual LLSD        keywords(bool lua) { return LLSD(); }
    virtual std::string definitionsVersion() { return std::string(); }
    // The LSL definitions as YAML, which say each function's categories
    // where the keywords do not; nothing to group by without them.
    virtual std::string definitionsYaml() { return std::string(); }
    // The preprocessor's own words while its transforms are on, which the
    // definitions do not list.
    virtual std::vector<std::string> preprocessorWords() { return {}; }
    // The LSL wiki's address for a page, with [LSL_STRING] for its name.
    virtual std::string lslHelpUrl() { return "[LSL_STRING]"; }

    // --- checking ----------------------------------------------------------

    // Whether the preprocessor runs, and whether a transform of its is on.
    virtual bool preprocessing() { return false; }
    virtual bool transformOn(ALPreprocessor::Transform transform) { return false; }
    // A script expanded as a save would send it, answered later.
    virtual void expand(ALScriptPreprocessor::Request request, std::function<void(const ALPreprocessor::Result&)> answer) {}
    // The .luaurc over a script, as far as it is known -- false where it is
    // not yet -- and fetched, told when it is.
    virtual bool configOf(const ALScriptPreprocessor::Request& request, ALLuauConfig& config, const ALLuauConfig* base) { return false; }
    virtual void fetchConfig(const ALScriptPreprocessor::Request& request, std::function<void()> fetched) {}
    // An include looked up where the preprocessor would look for it.
    virtual ALPreprocessor::Found lookUp(const ALScriptPreprocessor::Request& request, const ALPreprocessor::Ask& ask, ALPreprocessor::Include& found)
    {
        return ALPreprocessor::Found::No;
    }
    // The modules a script requires, as far as they are in hand, told
    // when more are; and those near fetched, told when they are.
    virtual std::vector<ALScriptModules::Module> modules(const ALScriptPreprocessor::Request& request, std::function<std::vector<ALScriptModules::Open>()> open,
                                                         const std::vector<std::string>& names, std::function<void()> ready)
    {
        return {};
    }
    virtual void fetchNearby(const ALScriptPreprocessor::Request& request, std::function<void()> fetched) {}
    // A SLua alias of the studio's own named for a folder on disk, and the
    // disk read (ALScriptPreprocessor::studioAliases): as a fix that moves a
    // require onto one does first. False where the name is another
    // folder's already.
    virtual bool nameStudioAlias(const std::string& name, const std::string& folder) { return false; }
};
