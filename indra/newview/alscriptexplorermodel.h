/**
 * @file alscriptexplorermodel.h
 * @brief What Script Studio's explorer lists: the objects in hand, their prims and what each holds, and the rows made of them.
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
#include "llsd.h"
#include "lluuid.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// What the Script Studio's explorer lists: the objects in hand -- pinned,
// selected in world, or holding a script that is open -- each prim's scripts
// and notecards as they are fetched, whether each script runs as the region
// last said, the pins, what is folded, and from all of that the rows a
// filter lets through. Nothing of the world: what an object in sight is and
// is called, the explorer's panel tells it.
class ALScriptExplorerModel
{
public:
    typedef ALScriptWorkspace::Item Item;

    // Past how many prims a linkset's prims are listed folded, what each
    // holds asked of the region only once it is shown: a build of 255 prims
    // is 255 downloads of what each holds, and a question for every script
    // in it of whether it runs.
    static constexpr size_t LARGE_LINKSET = 16;

    struct Prim
    {
        LLUUID            id;
        std::string       name;
        // The name is the prim's own, not a stand-in until it is heard.
        bool              named   = false;
        bool              fetched = false;
        std::vector<Item> items;
    };
    // A pinned object stays listed when it is neither selected nor holding
    // a script open, and across sessions; one that is not around is listed
    // by the name it had, with no prims.
    struct Object
    {
        LLUUID            root;
        std::string       name;
        bool              named   = false;
        bool              pinned  = false;
        bool              present = true;
        std::vector<Prim> prims;
    };
    struct Pin
    {
        LLUUID      root;
        std::string name;
    };

    // What a row chosen stands for: an object, a prim of one, or a script
    // or notecard in a prim.
    struct Choice
    {
        LLUUID      root;
        LLUUID      prim;
        LLUUID      item;
        std::string name;
        bool        script = false;
        bool        lua    = false;
        // A prim of a linkset's own row, rather than its object's.
        bool        primRow = false;
        bool        isItem() const { return item.notNull(); }
        ALScriptRef ref() const { return ALScriptRef(prim, item); }
        // By a row's value; none for a linkset's row of prims holding
        // nothing, which stands for no prim to act on or drop into.
        static std::optional<Choice> of(const LLSD& value);
    };

    // --- what is listed ------------------------------------------------------------

    // An object in sight as the world has it: each prim, the root first,
    // by the name it has been heard by, or none.
    struct Seen
    {
        struct Part
        {
            LLUUID      id;
            std::string name;
        };
        std::vector<Part> prims;
    };
    // What the objects in hand are listed from.
    struct Listing
    {
        // An object in sight, by any prim of it; none where it is not in
        // sight, or is an avatar.
        std::function<std::optional<Seen>(const LLUUID& id)> seen;
        // What to call an object whose name has not been heard.
        std::function<std::string(const LLUUID& root)> nameless;
        // The roots selected in world, in their order; the prims of the
        // scripts open.
        std::vector<LLUUID> selected;
        std::vector<LLUUID> open;
        // What a pin of an object not in sight, never named, is called.
        std::string unnamed;
    };
    // The objects in hand listed again: the pinned first, so that they keep
    // their place, one not in sight by its pin's name; then what is
    // selected; then the objects of the scripts open. What each prim was
    // known to hold is kept until it says again -- made afresh with nothing
    // in it, the list lost the row chosen in it and showed every object
    // empty until the answers came. A large linkset's prims are folded when
    // it is first listed, but the root, which usually holds what the object
    // does, and any with a script open. Names asked, and whether scripts
    // run, are let go of for prims no longer listed.
    void list(const Listing& listing);
    // The prims to ask what they hold: where it is not known, where the
    // world's word on it is not `current`, or every one where `refetch` --
    // a person asked, or something was made, renamed or deleted in one --
    // but not one asked and not answered yet, nor, of a large linkset, one
    // folded out of sight, unless `filtering` looks through it. Each is
    // taken as asked until it is answered.
    std::vector<LLUUID> toAsk(bool refetch, bool filtering, const std::function<bool(const LLUUID& prim)>& current);

    // A new item to be opened once its prim lists it: by its id, or its
    // name where the region gave no id; with the text it is opened with.
    void openWhenListed(const LLUUID& prim, const LLUUID& item, const std::string& name, std::optional<std::string> text);
    // What a prim holds, answered: a listing that did not come -- the
    // object never answered -- leaves what was known of it; one that did
    // says what it holds, and the prim's name. What it holds that is to be
    // asked whether it runs, and what was waiting for it to be listed, to
    // be opened now.
    struct Opening
    {
        ALScriptRef                ref;
        std::string                name;
        std::optional<std::string> text;
    };
    struct Heard
    {
        bool                     listed = false;
        std::vector<ALScriptRef> askRunning;
        std::vector<Opening>     opening;
    };
    Heard contents(const ALScriptWorkspace::Contents& contents);

    // --- names ---------------------------------------------------------------------

    // Whether a prim is listed, among which the world's word on a name is
    // looked for.
    bool listed(const LLUUID& prim) const { return mListedPrims.contains(prim); }
    // A name to be asked of its region, once while it is listed: true the
    // first time.
    bool askName(const LLUUID& prim);
    // The names read again, `nameOf` saying what a prim is called now, or
    // nothing where nothing has been heard; true where any changed.
    bool rereadNames(const std::function<std::string(const LLUUID& prim)>& nameOf);
    // A prim called something else here, at once -- the root its object too,
    // and the object's pin -- and its name to be asked again.
    void renamed(const LLUUID& prim, const std::string& name);
    // The pins' names changed since this was last asked: the object's latest
    // name is what a pin is remembered by.
    bool takePinsChanged() { return std::exchange(mPinsChanged, false); }

    // --- whether scripts run -------------------------------------------------------

    void                running(const ALScriptRef& ref, bool running) { mRunningKnown[{ ref.object, ref.item }] = running; }
    std::optional<bool> knownRunning(const ALScriptRef& ref) const;
    void                forgetRunning() { mRunningKnown.clear(); }
    const std::map<std::pair<LLUUID, LLUUID>, bool>& runningKnown() const { return mRunningKnown; }

    // --- the rows ------------------------------------------------------------------

    // A row: an object, a linkset's prim, the prims of a linkset holding
    // nothing, under one row after the rest, or a script or notecard. Its
    // value is what it stands for, which a chosen row is kept by.
    struct Row
    {
        enum class Kind : U8
        {
            Object,
            Prim,
            Empties,
            Item
        };
        Kind        kind = Kind::Object;
        LLSD        value;
        std::string name;
        bool        folded  = false;
        bool        pinned  = false;
        bool        present = true;
        // An object of more than one prim; an item of one, indented under
        // its prim's row.
        bool        many    = false;
        size_t      empties = 0;
        // Shown without the name the region gives it, which is to be asked.
        bool        unnamed = false;
        bool        script  = false;
        bool        lua     = false;
        ALScriptRef ref;
    };
    // The rows through a filter: an item whose name has its letters, and
    // what holds it; an object or a prim whose name has them, with all it
    // holds. A filter shows what it finds whatever is folded, and a prim by
    // its name, so the prims holding nothing are not put under one row
    // while it looks -- nor are they otherwise unless there are two or more,
    // and never the root.
    std::vector<Row> rows(const std::string& filter) const;

    // --- folding -------------------------------------------------------------------

    // What a fold asks of the list: nothing, filling again, or listing
    // again, what it now shows asked for where it was not.
    enum class Refold : U8
    {
        None,
        Refill,
        Relist
    };
    // An object's or a prim's rows folded shut or opened, by what it
    // stands for; a linkset's prims holding nothing, by its object, which
    // opened ask for nothing: what they hold is known already, which is how
    // they came to be there; and either by a row's value.
    Refold fold(const LLUUID& id, bool prim, std::optional<bool> folded = std::nullopt);
    Refold foldEmpties(const LLUUID& root, std::optional<bool> folded = std::nullopt);
    Refold foldRow(const LLSD& row, std::optional<bool> folded = std::nullopt);
    bool   folded(const LLUUID& root) const { return mFolded.contains(root); }
    // What holds a prim unfolded, for its rows to be shown; true where it
    // was never asked what it holds -- folded in a large linkset -- and is to
    // be asked now.
    bool   unfoldTo(const LLUUID& prim);

    // --- what the rows chosen reach ------------------------------------------------

    // The prims the rows chosen as prims or objects stand for, each once,
    // with the name the queues report under; an object row means every
    // prim of it. A script's own prim is not among them.
    std::vector<std::pair<LLUUID, std::string>> containerPrims(const std::vector<Choice>& rows) const;
    // The scripts chosen themselves, and every script of each prim or
    // object chosen, each once.
    S32 scriptsReached(const std::vector<Choice>& rows) const;
    // Whether a queue over these prims walks the row's script already.
    static bool walkedByQueue(const Choice& row, const std::vector<std::pair<LLUUID, std::string>>& prims);
    // Whether an object is in sight; what it is called.
    bool        present(const LLUUID& root) const;
    std::string nameOf(const LLUUID& root) const;
    // The prim a row is of, for a drop into it: an item's, a prim's own, an
    // object's root; none for the row of prims holding nothing.
    static LLUUID primOf(const LLSD& row);
    // The row a drop into a prim lights: the prim's, where its object shows
    // its prims, and the object's otherwise; none where it is not listed.
    LLSD dropRow(const LLUUID& prim) const;

    // --- pins ----------------------------------------------------------------------

    bool isPinned(const LLUUID& root) const;
    // Pinned or let go; the list is the caller's to bring up to date.
    void togglePinned(const LLUUID& root, const std::string& name);
    // Every object among the rows pinned if the first is not, else let go.
    void pin(const std::vector<Choice>& rows);
    const std::vector<Pin>& pins() const { return mPins; }
    void saveState(LLSD& state) const;
    void readState(const LLSD& state);

    const std::vector<Object>& objects() const { return mObjects; }

private:
    // An object's name, and its pin's with it.
    void renameObject(Object& object, const std::string& name);

    std::vector<Object>                       mObjects;
    std::vector<Pin>                          mPins;
    bool                                      mPinsChanged = false;
    // The objects and the prims of linksets folded shut, each by its id --
    // apart, since a linkset's root prim has its object's; and the linksets
    // whose prims holding nothing are listed, by root, folded under one row
    // otherwise.
    boost::unordered_flat_set<LLUUID>         mFolded;
    boost::unordered_flat_set<LLUUID>         mFoldedPrims;
    boost::unordered_flat_set<LLUUID>         mEmptiesOpen;
    // The prims asked what they hold and not answered yet, which are not
    // asked again meanwhile.
    boost::unordered_flat_set<LLUUID>         mContentsAsked;
    // The prims listed; those whose names were asked of their regions while
    // listed.
    boost::unordered_flat_set<LLUUID>         mListedPrims;
    boost::unordered_flat_set<LLUUID>         mNamesAsked;
    // What the region said runs, by prim and item.
    std::map<std::pair<LLUUID, LLUUID>, bool> mRunningKnown;
    // The new items to be opened once their prims list them.
    struct OpenWhenListed
    {
        LLUUID                     prim;
        LLUUID                     item;
        std::string                name;
        std::optional<std::string> text;
    };
    std::vector<OpenWhenListed>               mOpenWhenListed;
};
