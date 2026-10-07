/**
 * @file alscripttypes.h
 * @brief What the viewer knows a script by, and what the workspace answers about one.
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

#include "llinventory.h"
#include "llpointer.h"
#include "lluuid.h"

#include <boost/container_hash/hash.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class ALSourceMap;
class LLSD;

// Where a script lives: an item of the agent's inventory, or an item of an
// object's contents. The one identity for a script wherever the viewer
// meets it, so that the studio, the notecard window, the bridge and the
// compile queue agree about which script is which -- the floater keys and
// the bridge's subscription hash both come from it.
struct ALScriptRef
{
    // Null for the agent's inventory.
    LLUUID object;
    LLUUID item;

    ALScriptRef() = default;
    ALScriptRef(const LLUUID& object_in, const LLUUID& item_in) : object(object_in), item(item_in) {}

    bool inInventory() const { return object.isNull(); }
    bool isNull() const { return item.isNull(); }

    // The bridge's subscription id, which is also the name of the temp
    // file an external editor is given.
    std::string id() const;
    // A floater key, and back.
    LLSD               key() const;
    static ALScriptRef fromKey(const LLSD& key);

    friend bool operator==(const ALScriptRef& a, const ALScriptRef& b) { return a.object == b.object && a.item == b.item; }
    friend bool operator!=(const ALScriptRef& a, const ALScriptRef& b) { return !(a == b); }
    // For a map by it.
    friend size_t hash_value(const ALScriptRef& ref) noexcept
    {
        size_t seed = hash_value(ref.object);
        boost::hash_combine(seed, hash_value(ref.item));
        return seed;
    }
};

// What the workspace (ALScriptWorkspace) answers about a script, and what
// it is asked with: values alone, so that what only passes them on need
// not include the workspace.

// A script's language, and the target it compiles for.
struct ALScriptLanguage
{
    bool        lua = false;
    std::string compileTarget;
};

// A script's text, or a notecard's, as it was loaded.
struct ALScriptLoaded
{
    ALScriptRef      ref;
    LLUUID           assetId;
    std::string      name;
    std::string      text;
    ALScriptLanguage language;
    // Whether the agent may see the text at all, and change it.
    bool        viewable   = false;
    bool        modifiable = false;
    // A notecard rather than a script: its text with the format
    // stripped, and the items it carries, which go back with it.
    bool                                     notecard = false;
    std::vector<LLPointer<LLInventoryItem>> embedded;
    // Why there is no text, where there is none: in words, and in kind
    // -- the item or its object gone, the agent not permitted to see
    // it, the asset not there or not readable, or the fetch failing,
    // which may go through if it is tried again.
    enum class Failure : U8
    {
        None,
        Missing,
        NotPermitted,
        Unreadable,
        Fetch
    };
    std::string error;
    Failure     failure = Failure::None;
};

// What a saved item is: a script, compiled as it goes up, or a notecard.
enum class ALScriptKind : U8
{
    Script,
    Notecard
};
// Who sent a save -- the studio, VS Code through the bridge, the
// notecard window, a queue over an object, a recompile -- and which save
// it was: a tab waiting on its own save takes its own answer, not
// another's for the same item. A request of zero is given one as it is
// sent.
enum class ALScriptOrigin : U8
{
    Studio,
    Bridge,
    Editor,
    Queue,
    Recompile,
    // A file on disk that is the master of the script (ALScriptDiskMasters).
    Disk
};
struct ALScriptSender
{
    ALScriptSender(ALScriptOrigin origin_in = ALScriptOrigin::Studio, U64 request_in = 0) : origin(origin_in), request(request_in) {}
    ALScriptOrigin origin;
    U64            request;
};

// Zero-based line and column, as everything in the studio counts.
struct ALScriptDiagnostic
{
    S32         line   = 0;
    S32         column = 0;
    std::string level;
    std::string message;
    // The server gave a column: LSL's compiler does, Luau's does not.
    bool        hasColumn = false;
    // It names a line at all: a line the compiler said nothing of the
    // place of does not, nor an include that never came.
    bool        hasLine = true;
};

struct ALScriptCompileResult
{
    ALScriptRef             ref;
    bool                    success = false;
    bool                    running = false;
    // A notecard saved rather than a script compiled; and who sent it.
    ALScriptKind            kind = ALScriptKind::Script;
    ALScriptSender          sender;
    LLUUID                  newAssetId;
    // An inventory item the save made anew, where it made one.
    LLUUID                          newItemId;
    std::vector<ALScriptDiagnostic> diagnostics;
    // The server's lines as they came.
    std::vector<std::string> messages;
    // Why nothing was compiled, where nothing was; and whether that was
    // the region not saying what experience the script runs under.
    std::string error;
    bool        experienceUnknown = false;
    // Task scripts: the experience it was sent to run under -- the
    // null one for none -- which it runs under now.
    std::optional<LLUUID> experience;
    // Where it went up expanded, as its sender said (ALScriptSaveOptions):
    // the expansion's map, which the diagnostics are of once the
    // envelope's lines above the code -- `codeLine` of them -- are taken
    // off.
    std::shared_ptr<const ALSourceMap> sourceMap;
    S32                                codeLine = 0;

    // The diagnostics as places in the script's source, which stands from
    // `source_line` on in the text they are for: nought where that text is
    // the source, one where it is the envelope itself, whose source begins
    // on its second line -- and whose escaping moves the columns, which
    // are then dropped. Through the map where there is one; all as said
    // where there is none, the text having gone up as it was. A place in an
    // include, or in what the preprocessor made, is none in the source: it
    // names no line, and an include's name and line go before its message.
    std::vector<ALScriptDiagnostic> inSource(S32 source_line = 0) const;
};
typedef std::function<void(const ALScriptCompileResult&)> ALScriptCompileCallback;

struct ALScriptSaveOptions
{
    std::string compileTarget;
    // Task scripts only: whether it runs after the compile, and the
    // experience it runs under -- the null one for none. Not given,
    // each as it is now, asked of the region first: an upload says
    // both whatever, and one that guessed would start a stopped script
    // or take an experience away. Whether it runs is the index's word
    // where it has one; a region that does not answer in a few
    // seconds leaves it running, as a script newly saved is.
    std::optional<bool>   running;
    std::optional<LLUUID> experience;
    ALScriptSender        sender;
    // Where the text is an expansion in its envelope: the expansion's map,
    // and the line its code begins on, which the result carries to
    // everyone who hears of it, so that each can read the compiler's lines
    // back to the source.
    std::shared_ptr<const ALSourceMap> sourceMap;
    S32                                codeLine = 0;
};

// Something's text saved, whoever sent it: the text as it went up, its
// new asset, and who sent it -- a script's whether it compiled or not,
// since the text is up either way. Every editor hears it, and takes it
// where it holds the item, or asks whose to keep where it has changes
// of its own; the sender knows its own by the request.
struct ALScriptSaved
{
    ALScriptRef    ref;
    ALScriptKind   kind = ALScriptKind::Script;
    std::string    text;
    LLUUID         asset;
    ALScriptSender sender;
    bool           compiled = false;
    // The item the save made anew in place of `ref`'s, where it made one:
    // a notecard in an object.
    LLUUID         newItem;

    // What an editor holding the item does about a save of it heard from
    // elsewhere -- the author's text as it went up, a script's out of its
    // envelope, against what the editor holds: the same text, marked
    // saved; nothing typed there, or nothing that may be changed, taken as
    // if loaded afresh; typed there, and what went up is what the editor
    // last had -- a recompile, a queue -- kept, to be saved; typed there
    // otherwise, asked which to keep, since one would be lost. Every
    // editor of an item decides by this, so that a studio tab and the
    // notecard window decide alike.
    enum class Heard : U8
    {
        Same,
        Take,
        Keep,
        Ask
    };
    // `changed`: typed in, where it may be changed at all; `last_saved`:
    // the text as the editor last had it saved or loaded, where it knows,
    // asked only where it is needed.
    static Heard heard(std::string_view theirs, std::string_view here, bool changed,
                       const std::function<std::optional<std::string>()>& last_saved)
    {
        if (theirs == here)
        {
            return Heard::Same;
        }
        if (!changed)
        {
            return Heard::Take;
        }
        const std::optional<std::string> before = last_saved ? last_saved() : std::nullopt;
        return before && theirs == *before ? Heard::Keep : Heard::Ask;
    }
};

// A script's text as it goes up again (ALScriptWorkspace::prepare).
struct ALScriptPrepared
{
    std::string                     text;
    std::vector<ALScriptDiagnostic> errors;
    // Where it went into its envelope: the expansion's map, and the
    // line of the text the expanded code starts on.
    std::shared_ptr<const ALSourceMap> map;
    S32                                codeLine = 0;
};

// Seeing an item -- its text, or what an object holds -- or changing
// it or what its object holds.
enum class ALScriptRlvUse : U8
{
    See,
    Change
};

// Whether a script in an object runs and what it compiles for, as its
// region said.
struct ALScriptRunningState
{
    ALScriptRef ref;
    bool        running = false;
    std::string compileTarget;
};

// What a transfer of items from one prim into another moved, and what it
// did not.
struct ALScriptTransferResult
{
    S32                      moved = 0;
    // By name: what might not go, and never left the object; what came
    // to the agent's inventory and the object then would not take,
    // which is in the trash with the rest; and what never came through.
    std::vector<std::string> refused;
    std::vector<std::string> stranded;
    std::vector<std::string> lost;
    std::string              error;
};

// What a prim holds: its scripts and notecards, and the rest by name.
struct ALScriptContents
{
    // A script or a notecard in a prim's contents.
    struct Item
    {
        LLUUID      id;
        std::string name;
        bool        script = true;
        bool        lua    = false;
        // What the agent may do with it: copy, and change. A script is read
        // only with both; a notecard with copy alone, and only read without
        // modify.
        bool        copy   = true;
        bool        modify = true;
        std::string description;
    };

    // Anything else it holds, as a script names it: a sound to play, an
    // object to rez, a texture to put on a face.
    struct Other
    {
        std::string        name;
        LLAssetType::EType type = LLAssetType::AT_NONE;
    };

    LLUUID             prim;
    std::string        name;
    // False where the prim is not known here, or nothing came back;
    // listed as holding nothing where RLVa keeps its contents unseen.
    bool               fetched = false;
    std::vector<Item>  items;
    std::vector<Other> others;
    // The serial of the contents this is, which moves as they change
    // (LLViewerObject::getInventorySerial); -1 where it is not known.
    S32               serial = -1;
};

// A new script or notecard in a prim's contents, or why there is none.
struct ALScriptCreated
{
    LLUUID      prim;
    // The new item, or null where the region names it only through
    // the object's contents.
    LLUUID      item;
    std::string name;
    std::string error;
};

// What an object's scripts say on the debug channel and to their
// owner, as the viewer hears it -- and what the agent's own objects
// say aloud, to the agent alone, or in an IM: the lines that arrive
// together from one script are one event, and a run-time error is
// parsed to its place and its stack for both VMs. The line and the
// column are zero-based, or -1 where the message named none.
struct ALScriptRuntimeEvent
{
    enum class Channel : U8
    {
        Debug,
        OwnerSay,
        // Channel 0, heard by everyone near; llRegionSayTo to the
        // agent; llInstantMessage.
        Said,
        SaidTo,
        Instant
    };
    // Seconds since the epoch, as LLDate counts them.
    F64         time = 0.0;
    LLUUID      root;
    LLUUID      prim;
    // The script, where the message named one the prim holds.
    LLUUID      item;
    std::string objectName;
    // The prim that spoke, where it is not the object's root and the
    // object's own name is known: the object is then `objectName`.
    std::string primName;
    std::string scriptName;
    bool        lua     = false;
    Channel     channel = Channel::Debug;
    // The lines as they came.
    std::string message;
    bool        isError = false;
    std::string error;
    S32         line   = -1;
    S32         column = -1;
    std::vector<std::string> stack;
};
