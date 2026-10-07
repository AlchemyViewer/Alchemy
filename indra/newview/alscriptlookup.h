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

#include "alkeymap.h"
#include "alscriptanalysis.h"
#include "alscriptpreprocessor.h"
#include "alscriptreferencespane.h"
#include "alscriptstudiodoc.h"
#include "alscriptstudioplaces.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ALScriptStudioAnalysis;
class ALScriptNavigation;
class ALScriptStudioServices;
class ALScriptStudioTabs;

// The name being looked up across the object's scripts: what
// was asked, the script that declares it (this one, or the
// include, by identity) and where, how many scripts are still
// to answer, the places gathered so far, each once, and the
// version of each open script's text as it was read, so that a
// rename knows it still holds.
struct ALScriptStudioDoc::Lookup
{
    U32                            generation = 0;
    ALEditorCommand                command    = ALEditorCommand::None;
    std::string                    name;
    bool                           hasDefinition = false;
    std::string                    homePath;
    // This script and the home, where they are files on disk, by where
    // they stand once every link is followed; else as they are named.
    std::string                    sameOwn;
    std::string                    sameHome;
    ALScriptSpan                   definition;
    bool                           renamable = false;
    U32                            version   = 0;
    S32                            pending   = 0;
    std::vector<Place>             places;
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>      seen;
    boost::unordered_flat_map<std::string, U32, ll::string_hash, std::equal_to<>> versions;
    // What a rename would change, for its clash check: each script's
    // text as the analyzers read it -- this one's expansion, with its
    // includes, and each other script's reached -- by its name; and
    // the other scripts reached, by the path the map calls them.
    std::vector<std::pair<std::string, std::shared_ptr<const std::string>>> texts;
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>>  scripts;
    // What it could not look through: how many of the object's prims
    // did not say what they hold, and the scripts that could not be
    // read, by name.
    S32                                                                       unlisted = 0;
    std::vector<std::string>                                                  unread;
};

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

    // One of the object's other scripts a lookup may reach; or a file on
    // disk open in a tab, which no object or folder lists, by the path
    // the preprocessor calls it -- its tab's id -- where it is no item.
    struct Candidate
    {
        ALScriptRef ref;
        std::string name;
        std::string path;
        // A file on disk under a folder a script reads from, read from
        // there where no tab has it open; one open here is read only as
        // its tab has it.
        bool        onDisk = false;
        // Such a file's text, as the walk of the folders read it.
        std::shared_ptr<const std::string> text;
    };
    // All of them, and how many of the object's prims did not say what
    // they hold, whose scripts are not among them.
    struct Candidates
    {
        std::vector<Candidate> scripts;
        S32                    unlisted = 0;
    };
    // An inventory script's: the others of its folder's `items` in its
    // language, SLua's by the item's subtype or its runtime.
    static std::vector<Candidate> folderCandidates(const std::vector<const LLInventoryItem*>& items, const LLUUID& own, bool lua);

    // What the lookups ask of the window itself, beyond what they are given.
    class Window
    {
    public:
        // The other scripts of a tab's object in its language, while the
        // object is in sight, told once every prim of it has said what it
        // holds -- a large linkset's folded ones among them; of an
        // inventory script's folder, for one, at once.
        virtual void candidates(const Doc& doc, std::function<void(Candidates)> told) = 0;
        // Where a tab's lookup finds scripts on disk: the folders a script
        // of its language reads from -- the scripter's include folders,
        // what their configurations bless, the studio's aliases -- each by
        // its path, and none while disk includes are off. The lookup walks
        // them for the scripts of the language, off the main thread, since
        // any of them may require a module there, or include it.
        virtual std::vector<std::string> diskCandidates(const Doc& doc) { return {}; }
        // A script's text as the region has it, its author's source out of
        // any envelope, and its asset; nothing where it could not be read.
        virtual void loadSource(const ALScriptRef& ref, std::function<void(const LLUUID& asset, const std::optional<std::string>& source)> loaded) = 0;
        // A script expanded as the compiler sees it.
        virtual void expand(ALScriptPreprocessor::Request request, std::function<void(const ALPreprocessor::Result&)> expanded) = 0;
        // What Find References found: listed, lit in the script, the tab
        // brought up.
        virtual void showFound(Doc& doc, const Found& found) = 0;
        // The new name asked for, `hint` saying what Return will do with
        // what is typed; `chosen` with it, or `previewed` with Shift.
        virtual void askNewName(Doc& doc, std::function<std::string(const std::string& typed)> hint,
                                std::function<void(const std::string& name)> chosen,
                                std::function<void(const std::string& name)> previewed) = 0;
        // A rename shown before it is made: each place found, to be left
        // out or not, and `said` over them; `apply` with the places kept,
        // by their order in what was found.
        // `changes` with a file of the places, and those kept: what the
        // rename would make of that file shown.
        virtual void previewRename(Doc& doc, const Found& found, const std::string& new_name, const std::string& said,
                                   std::function<void(const std::vector<size_t>& kept)>                          apply,
                                   std::function<void(const std::string& file, const std::vector<size_t>& kept)> changes) = 0;
        // Two texts side by side in a tab's place, each under its title.
        virtual void compare(Doc& doc, const std::string& left, const std::string& right, const std::string& left_title,
                             const std::string& right_title) = 0;

    protected:
        ~Window() = default;
    };

    ALScriptLookup(ALScriptStudioServices& services, ALScriptStudioTabs& tabs, ALScriptStudioAnalysis& analysis, ALScriptNavigation& navigation, Window& window);
    ~ALScriptLookup();

    // What the preprocessor calls a tab's script, and its source map names
    // it by: an item's path, or a file's on disk, which is the tab's id.
    static std::string pathOf(const Doc& doc);
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
    // started in: at every place found, or at those `kept`, by their order.
    void renameTo(const std::string& id, U32 generation, const std::string& new_name, const std::vector<size_t>* kept = nullptr);
    // The same shown in the References tab first, each place to be left
    // out or not.
    void previewRename(const std::string& id, U32 generation, const std::string& new_name);
    // What that rename would make of one of its files -- the script it was
    // looked up from where `file` is empty -- at the places `kept`, beside
    // the file's text as it stands, in the file's tab. Only a tab the
    // rename would change on the spot, open here and unchanged since it was
    // read; false, said, for any other, whose places the tab lists.
    bool showRenameChanges(const std::string& id, U32 generation, const std::string& new_name, const std::string& file,
                           const std::vector<size_t>& kept);
    // Whether `name` stands in `text` as a name, outside its comments and
    // strings.
    static bool mentions(std::string_view text, std::string_view name, bool lua);
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
    // The files on disk a lookup reaches, each by where it stands.
    typedef boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> Listed;
    // What a walk of the folders on disk found: the scripts that mention
    // the name, each by where it stands, read; and the names of those that
    // could not be read.
    struct DiskRead
    {
        std::vector<std::pair<std::string, std::shared_ptr<const std::string>>> mention;
        std::vector<std::string>                                                unread;
    };
    // The folders on disk walked for a tab's lookup, and the scripts of
    // `lua`'s language under them read -- but those `listed`, which are
    // read as their tabs have them -- off the main thread where there is a
    // main loop to hand them back to; held for by the lookup until then.
    void walkDisk(const std::string& id, U32 generation, std::vector<std::string> folders, bool lua, const std::string& name, Listed listed);
    // The walk itself, on whichever thread: given up part way, between its
    // entries, once `stopped` says so.
    static DiskRead readDisk(const std::vector<std::string>& folders, bool lua, const std::string& name, const Listed& listed,
                             const std::function<bool()>& stopped);
    // What it found, back on the main thread: each script that mentions
    // the name begun as the lookup's others are, and the walk's hold let go;
    // and the walk, `walk` its stop, no longer under way.
    void walked(const std::string& id, U32 generation, DiskRead read, const std::shared_ptr<std::atomic<bool>>& walk);
    // As many of a tab's other scripts begun as may be on their way.
    void feed(const std::string& id);
    void begin(Doc& doc, U32 generation, const Candidate& candidate);
    // One of them done with, found in or passed over: the next begun, and
    // the lookup finished where it was the last.
    void passed(Doc& doc);
    void candidate(const std::string& id, U32 generation, const Candidate& other, const LLUUID& asset_id, std::shared_ptr<const std::string> text);
    void expanded(const std::string& id, U32 generation, const Candidate& other, const std::shared_ptr<const std::string>& source,
                  const ALPreprocessor::Result& result);
    // An answer read back through the expansion it was asked of: the
    // script's places through its map, and where SLua was read `apart`,
    // each module's through its own.
    void answered(const std::string& id, U32 generation, const Candidate& other, const ALPreprocessor::Result& expansion, bool apart,
                  const std::string& source, const std::shared_ptr<const std::string>& expanded, const ALScriptAnalysis::Result& result);
    void settled(Doc& doc);
    // What Return does with a name typed for a rename of `old_name`, found
    // at `count` places in `scripts` scripts, said.
    // Under the prompt, `prompting`, it says Shift-Return previews.
    std::string renameHint(const Doc& doc, const std::string& old_name, S32 count, S32 scripts, const std::string& typed, bool prompting = true) const;
    // Why a name typed cannot be renamed to, said; empty where it can.
    std::string refused(const Doc& doc, const std::string& name) const;
    // The script a rename's texts name `name` in already, where one does.
    std::string clashIn(const Doc& doc, const std::string& name) const;
    // What a rename cannot reach, said: the scripts of other objects that
    // share an include it changes. Empty where it changes no include.
    std::string unreached(const Doc& doc) const;
    // What a lookup could not look through, said: the object's prims that
    // did not say what they hold, and the scripts that could not be read.
    // Empty where it looked through all.
    std::string passedOver(const Doc& doc) const;
    // The tab of a lookup of this generation, where it is still open.
    Doc* lookingIn(const std::string& id, U32 generation);

    ALScriptStudioServices& mServices;
    ALScriptStudioTabs&     mTabs;
    ALScriptStudioAnalysis& mAnalysis;
    ALScriptNavigation&     mNavigation;
    Window&                 mWindow;
    // Which lookup the answers arriving belong to.
    U32                     mGeneration = 0;
    // Each tab's lookup's other scripts, by the tab.
    boost::unordered_flat_map<std::string, Lane, ll::string_hash, std::equal_to<>> mLanes;
    // Held while this is, for an answer to know it still is.
    std::shared_ptr<bool>   mAlive = std::make_shared<bool>(true);
    // Each tab's lookup's walk of the folders on disk under way, by the tab:
    // what tells it to give up, raised as the tab's lookup is begun again,
    // and as this goes.
    boost::unordered_flat_map<std::string, std::shared_ptr<std::atomic<bool>>, ll::string_hash, std::equal_to<>> mWalks;
};
