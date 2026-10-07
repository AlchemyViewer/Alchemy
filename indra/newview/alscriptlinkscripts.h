/**
 * @file alscriptlinkscripts.h
 * @brief Every script of the prims chosen in Script Studio's Explorer, each proposed a file on disk to be linked to as its master.
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

#include "aldiskincludes.h"
#include "almasterlinks.h"
#include "alscripttypes.h"
#include "lluuid.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct ALScriptLoaded;

// Every script of the prims chosen in the Explorer, each proposed a file on
// disk to be its master, for the scripter to link them all at once (Link
// Scripts to Files):
// - each prim asked what it holds, of its region, since an object's own
//   copy hears nothing of what a co-owner saved;
// - each script's text read through the workspace, a few at a time;
// - a file found for each, off the main thread as the studio's other looks
//   at the disk are: the one the script's own @file names, where an include
//   of it could read it (ALMasterMatch::resolve); else the file an earlier
//   link of an item of its name had, the item having gone with a take and
//   come back rezzed; else those of its name under the folders an include
//   may be read from (ALMasterMatch::byName). More than one is a choice
//   for the scripter, never chosen for them;
// - each file found asked what a send of it would make beside what the
//   world holds (ALScriptMasterUpload::probe), a few at a time: the same,
//   different, or not to be told.
// What is proposed, chosen and ticked is the link pane's to show; its Link
// makes the links of what is ticked here. Begun again, or let go of, what
// the last one had under way finds nobody.
class ALScriptLinkScripts
{
public:
    // How a script's file was found: by the script's own @file, by an
    // earlier link's record, by the item's name, or picked by the scripter.
    enum class How : U8
    {
        None,
        Hint,
        Record,
        Name,
        Picked
    };
    // What the world holds beside what the file would send: not asked
    // yet, being asked, the same, different, or not to be told.
    enum class World : U8
    {
        Unasked,
        Asking,
        Same,
        Differs,
        Unknown
    };
    // How far it has come.
    enum class Stage : U8
    {
        Idle,
        Listing,
        Reading,
        Matching,
        Done
    };

    // A prim whose scripts are listed: its object by its root, what that is
    // called, and where the prim is in words.
    struct Prim
    {
        LLUUID      id;
        LLUUID      root;
        std::string object;
        std::string place;
    };

    // A script, and the file proposed for it.
    struct Row
    {
        ALScriptRef ref;
        LLUUID      root;
        std::string name;
        std::string place;
        std::string objectName;
        std::string regionName;
        // Whether the agent owns its object: a stranger's script naming a
        // file is never ticked to be linked to it unasked.
        bool        owned = false;
        bool        lua   = false;
        std::string target;
        LLUUID      asset;
        // The file the script's own @file names, as it names it, and why
        // that reached no file, where it did not.
        std::string hint;
        std::string hintWhy;
        // The files it may be, best first, where more than one was found,
        // and how they were: the scripter's to choose from.
        std::vector<std::string> choices;
        How                      choicesHow = How::None;
        // What each earlier link among the choices was made by, where they
        // are records.
        std::vector<ALMasterLink::Made> choicesMade;
        // The file proposed or chosen, and how it was found; what an
        // earlier link it was found by was made by.
        std::string        file;
        How                how  = How::None;
        ALMasterLink::Made made = ALMasterLink::Made::Name;
        // Found by the script's @file, and not called what the item is:
        // the script chose it, which the scripter is to see.
        bool               named = false;
        // What the world holds beside it, why where it cannot be told, and
        // what a send would make of it, hashed as a link keeps it.
        World       world = World::Unasked;
        std::string worldWhy;
        std::string ours;
        bool        ticked = false;
        // Which ask of the world is the last, for the answer to a file
        // since changed to be let go of.
        U32         asked = 0;
    };

    // How many scripts there were, and what of them is not listed: those
    // linked already, those that could not be read, and the prims that did
    // not say what they hold.
    struct Tally
    {
        S32 scripts    = 0;
        S32 toRead     = 0;
        S32 read       = 0;
        S32 linked     = 0;
        S32 unreadable = 0;
        S32 unlisted   = 0;
    };

    // Told whenever the rows, or how far it has come, changed.
    explicit ALScriptLinkScripts(std::function<void()> changed);
    ~ALScriptLinkScripts();

    // How many scripts are read, and how many sends asked about, at once.
    static constexpr S32 AT_ONCE = 4;

    // Every script of the prims, as above; the last let go of.
    void start(std::vector<Prim> prims);
    // Let go of: nothing listed, and nothing under way heard.
    void cancel();

    Stage                   stage() const { return mStage; }
    bool                    running() const { return mStage != Stage::Idle && mStage != Stage::Done; }
    const std::vector<Row>& rows() const { return mRows; }
    const Tally&            tally() const { return mTally; }
    // What the objects listed are called, each once, in order.
    const std::vector<std::string>& objects() const { return mObjects; }
    // Whether the world is still being asked about any of them.
    bool asking() const;

    // A row's file chosen -- one of its choices, or one picked -- which the
    // world is asked about again, and the row ticked: the scripter chose
    // it. False where the file is not a script of the row's language.
    bool choose(size_t index, const std::string& file, How how);
    void tick(size_t index, bool ticked);

    // The link a row makes, as matched, its base the item's asset as it
    // was read; where the world holds what the file makes, with that and
    // the file's stamp as of now, so that it is not sent again for nothing.
    ALMasterLink linkOf(const Row& row, S64 stamp) const;

    // What Link made: each script linked, where it is, its file, and
    // whether it was sent from it.
    struct Linked
    {
        struct One
        {
            std::string name;
            std::string place;
            std::string file;
            bool        sent = false;
        };
        std::vector<One> ones;
    };
    // The rows ticked linked (linkOf), the stamps of the files the world
    // holds already looked at off the main thread first; then, where
    // `send_differing`, those that differ sent from their files as the
    // studio's own send, which a change in the world since holds. Told
    // what was made once it is, and what was proposed let go of.
    void link(bool send_differing, std::function<void(const Linked&)> done);
    bool linking() const { return mLinking; }

    // Whether a file is called what an item is, in any case, a script's
    // extension aside on both.
    static bool sameName(const std::string& file, const std::string& item_name);

private:
    // What a row asks of the disk, and what it was found to be, as the
    // thread sees them: copies, nothing of the world.
    struct Ask
    {
        std::string                                             hint;
        bool                                                    lua = false;
        std::string                                             name;
        std::vector<std::pair<std::string, ALMasterLink::Made>> records;
    };
    struct Found
    {
        std::string              file;
        How                      how  = How::None;
        ALMasterLink::Made       made = ALMasterLink::Made::Name;
        std::vector<std::string>        choices;
        How                             choicesHow = How::None;
        std::vector<ALMasterLink::Made> choicesMade;
        std::string                     hintWhy;
    };
    // The folders a hint or a name may reach for a language, and its
    // aliases: read on the main thread, which alone may read the settings.
    struct Reach
    {
        ALDiskIncludes                                   blessed;
        std::vector<std::pair<std::string, std::string>> aliases;
    };
    static std::vector<Found> match(const std::vector<Ask>& asks, const Reach& lsl, const Reach& lua, const std::function<bool()>& stopped);

    void listed(U32 generation, std::vector<Prim> prims, std::vector<LLUUID> unlisted);
    void feed();
    void read(U32 generation, size_t index, const ALScriptLoaded& loaded);
    void findFiles();
    void found(U32 generation, std::vector<Found> found);
    void feedAsks();
    void ask(size_t index);

    std::function<void()>    mChanged;
    U32                      mGeneration = 0;
    Stage                    mStage      = Stage::Idle;
    Tally                    mTally;
    std::vector<std::string> mObjects;
    std::vector<Row>         mRows;
    // The scripts to read, and their text's lead as read: the @file is
    // found from it.
    struct Reading
    {
        ALScriptRef ref;
        std::string name;
        LLUUID      root;
        std::string place;
        std::string objectName;
        std::string regionName;
        bool        owned = false;
    };
    std::vector<Reading>            mToRead;
    std::vector<std::optional<Row>> mReadRows;
    size_t                   mNextRead = 0;
    S32                      mReading  = 0;
    bool                     mFeeding  = false;
    // How many rows the world is being asked about.
    S32                      mAsking      = 0;
    bool                     mFeedingAsks = false;
    bool                     mLinking     = false;
    // Told to give up, the look at the disk under way.
    std::shared_ptr<std::atomic<bool>> mStop;
    // Held while this is, for an answer to know it still is.
    std::shared_ptr<bool>    mAlive = std::make_shared<bool>(true);
};
