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
#include <string>
#include <string_view>
#include <vector>

class LLChat;
class LLEventTimer;
class LLInventoryItem;
class LLMessageSystem;

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
        // Why nothing was compiled, where nothing was.
        std::string error;
    };
    typedef std::function<void(const CompileResult&)> compile_callback_t;

    struct SaveOptions
    {
        std::string compileTarget;
        // Task scripts only: whether it runs after the compile, and the
        // experience it runs under.
        bool   running = true;
        LLUUID experience;
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

    // --- a script in an object -----------------------------------------------

    bool setRunning(const ALScriptRef& ref, bool running);
    bool reset(const ALScriptRef& ref);

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
        // False where the prim is not known here, or nothing came back.
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
};
