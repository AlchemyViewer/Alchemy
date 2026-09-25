/**
 * @file alscriptworkspace.h
 * @brief The scripts the viewer can reach: one way to name, load, save and compile them.
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

#include "llassettype.h"
#include "llinventory.h"
#include "llpointer.h"
#include "llsd.h"
#include "llsingleton.h"
#include "lluuid.h"

#include <boost/signals2.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class LLChat;
class LLEventTimer;
class LLInventoryItem;
class LLMessageSystem;
class LLViewerObject;

// Where a script lives: an item of the agent's inventory, or an item of an
// object's contents. The one identity for a script wherever the viewer
// meets it, so that the studio, the legacy floaters, the bridge and the
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
};

// What a script is and what happens to it: its language and compile target
// decided once, its text fetched, its text uploaded and compiled with the
// result parsed into the one diagnostic convention, the running state and
// reset of a script in an object, and what scripts say as they run. The
// legacy floaters decided each of these for themselves, four times over
// for the compile target; the studio and the bridge share this instead.
// Every answer arrives on the main thread.
class ALScriptWorkspace : public LLSingleton<ALScriptWorkspace>
{
    LLSINGLETON(ALScriptWorkspace);
    ~ALScriptWorkspace() override;

public:
    // A script's language, and the target it compiles for: the item's
    // subtype and runtime where the item says, then what the text looks
    // like. `requested` is a target somebody asked for, which an LSL script
    // asked to run on Luau turns into the LSL-on-Luau target.
    struct Language
    {
        bool        lua = false;
        std::string compileTarget;
    };
    static Language resolve(const LLInventoryItem* item, std::string_view content, const std::string& requested = std::string());
    // No `default` state anywhere means Lua: the same guess the legacy
    // editor made.
    static bool looksLikeLua(std::string_view content);

    // The text of an asset the storage has fetched, from the cache, cut
    // at the first NUL as the legacy readers cut it.
    static bool readAsset(const LLUUID& asset_id, LLAssetType::EType type, std::string& out);

    // --- loading ---------------------------------------------------------

    struct Loaded
    {
        ALScriptRef ref;
        LLUUID      assetId;
        std::string name;
        std::string text;
        Language    language;
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
    typedef std::function<void(const Loaded&)> load_callback_t;

    // The script's text, fetched if need be. A script the agent may not
    // view comes back without text and says so. A notecard loads the same
    // way, its text alone.
    void load(const ALScriptRef& ref, load_callback_t callback);

    // --- saving and compiling ----------------------------------------------

    // Zero-based line and column, as everything in the studio counts.
    struct Diagnostic
    {
        S32         line   = 0;
        S32         column = 0;
        std::string level;
        std::string message;
        // The server gave a column: LSL's compiler does, Luau's does not.
        bool        hasColumn = false;
    };

    struct CompileResult
    {
        ALScriptRef             ref;
        bool                    success = false;
        bool                    running = false;
        // A notecard saved rather than a script compiled.
        bool                    notecard = false;
        LLUUID                  newAssetId;
        std::vector<Diagnostic> diagnostics;
        // The server's lines as they came.
        std::vector<std::string> messages;
        // Why nothing was compiled, where nothing was; and whether that was
        // the region not saying what experience the script runs under.
        std::string error;
        bool        experienceUnknown = false;
        // Task scripts: the experience it was sent to run under -- the
        // null one for none -- which it runs under now.
        std::optional<LLUUID> experience;
    };
    typedef std::function<void(const CompileResult&)> compile_callback_t;

    struct SaveOptions
    {
        std::string compileTarget;
        // Task scripts only: whether it runs after the compile, and the
        // experience it runs under -- the null one for none. Not given,
        // the one it runs under now, asked of the region first: an
        // upload says an experience whatever, and one that said none
        // would take it away.
        bool                  running = true;
        std::optional<LLUUID> experience;
    };

    // Uploads and compiles. False, with why and nothing sent, where there
    // is no region, no capability or no such object; the result otherwise
    // reaches the callback and every listener.
    bool save(const ALScriptRef& ref, const std::string& text, const SaveOptions& options, compile_callback_t callback, std::string& error);

    // A notecard's text uploaded in its format with the items it carried;
    // the result says it was saved, or why not, the same way.
    bool saveNotecard(const ALScriptRef& ref, const std::string& text, const std::vector<LLPointer<LLInventoryItem>>& embedded,
                      compile_callback_t callback, std::string& error);

    // A script's text as it goes up again: expanded afresh from its
    // source where the preprocessor wrapped it or is on, so that its
    // includes are current, and wrapped again; or as it is. The
    // preprocessor's errors, where it found any, are the diagnostics and
    // nothing goes up.
    struct Prepared
    {
        std::string             text;
        std::vector<Diagnostic> errors;
    };
    typedef std::function<void(const Prepared&)> prepared_callback_t;
    void prepare(const ALScriptRef& ref, const std::string& name, const LLUUID& asset_id, const std::string& text, bool lua,
                 const std::string& target, prepared_callback_t callback);

    // A script fetched, prepared, and uploaded to compile for a target:
    // what was asked, or "auto" for what it compiles for now. What the
    // compiler said reaches the callback and every listener; a script
    // that cannot go up says why in the result's error.
    void recompile(const ALScriptRef& ref, const std::string& target, compile_callback_t callback);

    // The server's error strings as diagnostics.
    static std::vector<Diagnostic> parseDiagnostics(const LLSD& errors, bool lua);

    typedef boost::signals2::signal<void(const CompileResult&)> compiled_signal_t;
    boost::signals2::connection onCompiled(const compiled_signal_t::slot_type& slot) { return mCompiled.connect(slot); }

    // --- what RLVa allows ----------------------------------------------------

    // Seeing an item -- its text, or what an object holds -- or changing
    // it or what its object holds.
    enum class RlvUse : U8
    {
        See,
        Change
    };
    // Why RLVa refuses a use, in its own words, or nothing where it allows
    // it, as the viewer's own windows have it: no script seen under
    // @viewscript nor notecard under @viewnote, wherever it is; and in an
    // object, nothing seen or changed where @edit or @editobj keeps the
    // build floater -- whose Contents are where an object's items are --
    // from the object, nor in a locked attachment. `object` is null for an
    // item in the inventory; `type` is the item's, or AT_NONE for what the
    // object holds as a whole. The studio, the compile queues and the
    // external editors' bridge all ask it.
    static std::string rlvRefusal(LLViewerObject* object, LLAssetType::EType type, RlvUse use);

    // --- a script in an object -----------------------------------------------

    bool setRunning(const ALScriptRef& ref, bool running);
    bool reset(const ALScriptRef& ref);
    // Stopped, then set running again once its region says it has stopped:
    // sent back to back, a stop sent again after it went astray could reach
    // the region after the start and leave the script stopped. Asked again
    // while the region says it still runs, and started after a while all
    // the same. False where the stop could not be sent.
    bool restart(const ALScriptRef& ref);

    // Whether a script in an object runs and what it compiles for, asked
    // of the region; every listener hears the answer. False where there
    // is nobody to ask.
    struct RunningState
    {
        ALScriptRef ref;
        bool        running = false;
        std::string compileTarget;
    };
    bool askRunning(const ALScriptRef& ref);
    typedef boost::signals2::signal<void(const RunningState&)> running_signal_t;
    boost::signals2::connection onRunningState(const running_signal_t::slot_type& slot) { return mRunningState.connect(slot); }
    // The region's answer, registered for the message; the legacy live
    // editor hears it through here.
    static void processScriptRunningReply(LLMessageSystem* msg, void** data);

    // The experience a script in an object runs under, asked of its
    // region: the null one for none -- and for a script in the inventory,
    // or a region that keeps no experiences -- and nothing where the
    // region could not be asked, or did not answer.
    typedef std::function<void(const std::optional<LLUUID>&)> experience_callback_t;
    void askExperience(const ALScriptRef& ref, experience_callback_t told);

    // The experiences the agent may put a script under, as the region
    // lists them: asked of it once, and each asker told once they are in.
    // Empty until then, and where the agent has none.
    typedef std::function<void(const std::vector<LLUUID>&)> experiences_callback_t;
    void                       askOwnExperiences(experiences_callback_t told);
    const std::vector<LLUUID>& ownExperiences() const { return mOwnExperiences; }

    // --- between objects ------------------------------------------------------------

    // Items of one prim put into another. The region lets nothing go from
    // one object into another but through the agent's inventory: each is
    // taken into a folder made for it in the trash -- a copy where it may
    // be copied, the item itself out of an object of one's own -- and put
    // from there into the other prim, a script running or not as asked.
    // A copy that went in stays in that folder, in the trash, where the
    // agent can see what passed through; what the other prim would not
    // take stays there too, for the agent to take back. One transfer runs
    // at a time, the rest waiting their turn, and each takes from the
    // folder only what it is waiting for: a transfer knows its items there
    // by their names alone.
    struct TransferResult
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
    typedef std::function<void(const TransferResult&)> transfer_callback_t;
    void transfer(const LLUUID& from, const std::vector<LLUUID>& items, const LLUUID& to, bool running, transfer_callback_t done);

    // --- what an object holds ----------------------------------------------------

    // A script or a notecard in a prim's contents.
    struct Item
    {
        LLUUID      id;
        std::string name;
        bool        script = true;
        bool        lua    = false;
    };
    struct Contents
    {
        LLUUID            prim;
        std::string       name;
        // False where the prim is not known here, or nothing came back;
        // listed as holding nothing where RLVa keeps its contents unseen.
        bool              fetched = false;
        std::vector<Item> items;
    };
    typedef std::function<void(const Contents&)> contents_callback_t;
    // The scripts and notecards a prim holds, fetched from the region if
    // need be, answered once on the main thread: as not fetched where the
    // region has not answered within a while.
    void listContents(const LLUUID& prim, contents_callback_t callback);

    // --- changing what an object holds ---------------------------------------------

    struct Created
    {
        LLUUID      prim;
        // The new item, or null where the region names it only through
        // the object's contents.
        LLUUID      item;
        std::string name;
        std::string error;
    };
    typedef std::function<void(const Created&)> created_callback_t;

    // A new script, in one language and from the region's template, or a
    // new notecard, in a prim's contents. False with why where nothing was
    // asked for; the answer otherwise reaches the callback on the main
    // thread.
    bool create(const LLUUID& prim, bool notecard, bool lua, const std::string& name, created_callback_t callback, std::string& error);
    // A new name for a script or notecard, in an object or in inventory.
    bool rename(const ALScriptRef& ref, const std::string& name, std::string& error);
    // An item taken out of an object, which is for good, or an inventory
    // item put in the trash.
    bool remove(const ALScriptRef& ref, std::string& error);

    // The legacy queues over whole prims, which walk their contents and
    // report in a window of their own: every script recompiled for a
    // target ("auto" for what each compiles for now), reset, started or
    // stopped. Each prim comes with a name for the report.
    enum class Queue : U8
    {
        Recompile,
        Reset,
        Start,
        Stop
    };
    bool queue(Queue kind, const std::vector<std::pair<LLUUID, std::string>>& prims, const std::string& target, std::string& error);

    // --- what scripts say ------------------------------------------------------

    // What an object's scripts say on the debug channel and to their
    // owner, as the viewer hears it: the lines that arrive together from
    // one script are one event, and a run-time error is parsed to its
    // place and its stack for both VMs. The line and the column are
    // zero-based, or -1 where the message named none.
    struct RuntimeEvent
    {
        enum class Channel : U8
        {
            Debug,
            OwnerSay
        };
        // Seconds since the epoch, as LLDate counts them.
        F64         time = 0.0;
        LLUUID      root;
        LLUUID      prim;
        // The script, where the message named one the prim holds.
        LLUUID      item;
        std::string objectName;
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
    // Every debug-channel and owner-say line the viewer hears from an
    // object goes through here. A run-time error's lines arrive one
    // message at a time; they are joined for a moment before they are
    // delivered.
    void ingestChat(const LLChat& chat);
    // Whatever is still being joined, delivered as it is.
    void flushRuntime();
    // The last few hundred events, oldest first, for a pane opened late.
    const std::deque<RuntimeEvent>& recentRuntime() const { return mRecent; }

    typedef boost::signals2::signal<void(const RuntimeEvent&)> runtime_signal_t;
    boost::signals2::connection onRuntime(const runtime_signal_t::slot_type& slot) { return mRuntime.connect(slot); }

private:
    struct Burst;
    struct ContentsListener;
    struct Transfer;
    void startTransfer(const std::shared_ptr<Transfer>& transfer);
    void transferArrived(const std::shared_ptr<Transfer>& transfer);
    void transferEnd(const std::shared_ptr<Transfer>& transfer);
    void flushExpiredBurst();
    void deliverRuntime(const Burst& burst);
    void sweepListeners();

    struct LoadRequest;
    static void onAssetLoaded(const LLUUID& asset_id, LLAssetType::EType type, void* user_data, S32 status, LLExtStat ext_status);
    void        deliver(const CompileResult& result, const compile_callback_t& callback);
    bool        scriptMessage(const ALScriptRef& ref, const char* message, bool running, bool with_running);

    compiled_signal_t             mCompiled;
    running_signal_t              mRunningState;
    // Shared, so that the timer answering one the object never did can
    // tell whether it is still here.
    std::vector<std::shared_ptr<ContentsListener>> mListeners;
    std::unique_ptr<Burst>        mBurst;
    std::unique_ptr<LLEventTimer> mBurstTimer;
    std::deque<RuntimeEvent>      mRecent;
    runtime_signal_t              mRuntime;
    // The one in front is under way; the rest wait for it to end.
    std::vector<std::shared_ptr<Transfer>> mTransfers;
    std::vector<LLUUID>                 mOwnExperiences;
    std::vector<experiences_callback_t> mOwnExperiencesWaiting;
    bool                                mOwnExperiencesAsked = false;
    bool                                mOwnExperiencesKnown = false;
};
