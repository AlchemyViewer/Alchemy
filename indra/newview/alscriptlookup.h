/**
 * @file alscriptlookup.h
 * @brief Script Studio's lookups across the object's scripts: references found, a name renamed, and the References tab.
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

#include "alscriptanalysis.h"
#include "alscriptpreprocessor.h"
#include "alscriptreferencespane.h"
#include "alscriptstudiodoc.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class ALScriptStudioServices;

// A Script Studio window's lookups of a name across scripts, as the tab's
// part `doc.lookup` keeps one: Find References and Rename, from what the
// analyzers found in the script, then through every other script of its
// object -- or its inventory folder -- in the same language that may share
// the name -- each read as it
// stands in an open tab or as the region has it, passed over where it does
// not mention the name, expanded as the compiler sees it, and asked of the
// analyzers. What comes back is listed in the References tab, or renamed:
// in this script as one step, in another open one unchanged since it was
// read, and in one not open with the change waiting for its text. An
// answer to an earlier lookup is dropped.
class ALScriptLookup
{
public:
    typedef ALScriptStudioDoc            Doc;
    typedef ALScriptReferencesPane::Found Found;

    // One of the object's other scripts a lookup may reach.
    struct Candidate
    {
        ALScriptRef ref;
        std::string name;
    };
    // An inventory script's: the others of its folder's `items` in its
    // language, SLua's by the item's subtype or its runtime.
    static std::vector<Candidate> folderCandidates(const std::vector<const LLInventoryItem*>& items, const LLUUID& own, bool lua);

    // What the lookups ask of the window beyond its services.
    class Window
    {
    public:
        // The other scripts of a tab's object in its language, while the
        // object is in sight; of an inventory script's folder, for one.
        virtual std::vector<Candidate> candidates(const Doc& doc) = 0;
        // A script's text as the region has it, its author's source out of
        // any envelope, and its asset; empty where it could not be read.
        virtual void loadSource(const ALScriptRef& ref, std::function<void(const LLUUID& asset, const std::string& source)> loaded) = 0;
        // A script expanded as the compiler sees it; and the analyzers asked.
        virtual void expand(ALScriptPreprocessor::Request request, std::function<void(const ALPreprocessor::Result&)> expanded) = 0;
        virtual void askAnalysis(ALScriptAnalysis::Request request, std::function<void(const ALScriptAnalysis::Result&)> answered) = 0;
        // A line of an include as it reads; false where it is not had.
        virtual bool sourceLine(const std::string& path, S32 line, std::string& out) const = 0;
        // A preview held, so that following what is found does not close it.
        virtual void holdPreview(Doc& doc) = 0;
        // What Find References found: listed, lit in the script, the tab
        // brought up.
        virtual void showFound(Doc& doc, const Found& found) = 0;
        // The new name asked for, `hint` saying what Return will do with
        // what is typed; `chosen` with it.
        virtual void askNewName(Doc& doc, std::function<std::string(const std::string& typed)> hint,
                                std::function<void(const std::string& name)> chosen) = 0;
        // A file on disk opened in a tab here; a tab brought forward.
        virtual Doc* openFileTab(const std::string& path, bool lua) = 0;
        virtual void activate(Doc& doc)                             = 0;

    protected:
        ~Window() = default;
    };

    ALScriptLookup(ALScriptStudioServices& services, Window& window);

    // A place added, once: by its file and where it starts.
    static void addPlace(Doc::Lookup& lookup, Doc::Place place);
    // The script's own first, then each other script's, by name, then in
    // order.
    static void sortPlaces(std::vector<Doc::Place>& places);
    // Whether a name is the language's own, which a name of the script's
    // cannot be: a keyword or type, the preprocessor's words while their
    // transforms are on, a word the definitions give.
    static bool reserved(bool lua, const std::string& name);

    // How many of a lookup's other scripts are read, expanded and asked
    // about at once: a fan-out over an object of fifty scripts would
    // otherwise put fifty reads, expansions and questions ahead of the
    // tab's own on threads of one.
    static constexpr S32 AT_ONCE = 2;

    // A lookup started from what the analyzers found in a tab.
    void start(Doc& doc, ALEditorCommand command, const ALScriptReferences& refs, bool has_definition, const std::string& home_path,
               const ALScriptSpan& definition, std::vector<Doc::Place> places, U32 version);
    // The rename to a name, from the tab a lookup of this generation was
    // started in.
    void renameTo(const std::string& id, U32 generation, const std::string& new_name);
    // The edits a rename left waiting for a tab's text, made where the old
    // name still stands; the places missed said.
    void applyPendingEdits(Doc& doc);

private:
    // A tab's lookup's other scripts: those still to begin, and how many
    // are on their way. A lookup begun again in the tab lets go of the
    // rest of the last one's.
    struct Lane
    {
        U32                    generation = 0;
        std::vector<Candidate> left;
        size_t                 next    = 0;
        S32                    running = 0;
        bool                   feeding = false;
    };
    // As many of a tab's other scripts begun as may be on their way.
    void feed(const std::string& id);
    void begin(Doc& doc, U32 generation, const Candidate& candidate);
    // One of them done with, found in or passed over: the next begun, and
    // the lookup finished where it was the last.
    void passed(Doc& doc);
    void candidate(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const LLUUID& asset_id,
                   std::shared_ptr<const std::string> text);
    void expanded(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name,
                  const std::shared_ptr<const std::string>& source, const ALPreprocessor::Result& result);
    void answered(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const ALSourceMap& map,
                  const std::string& source, const std::string& expanded, const ALScriptAnalysis::Result& result);
    void settled(Doc& doc);
    // What Return does with a name typed for a rename of `old_name`, found
    // at `count` places in `scripts` scripts, said.
    std::string renameHint(const Doc& doc, const std::string& old_name, S32 count, S32 scripts, const std::string& typed) const;
    // The tab of a lookup of this generation, where it is still open.
    Doc* lookingIn(const std::string& id, U32 generation);

    ALScriptStudioServices& mServices;
    Window&                 mWindow;
    // Which lookup the answers arriving belong to.
    U32                     mGeneration = 0;
    // Each tab's lookup's other scripts, by the tab.
    boost::unordered_flat_map<std::string, Lane, ll::string_hash, std::equal_to<>> mLanes;
    // Held while this is, for an answer to know it still is.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
};
