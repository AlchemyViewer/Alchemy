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

#include "alscripttypes.h"

#include "llassettype.h"
#include "llextendedstatus.h"
#include "llinventory.h"
#include "llpointer.h"
#include "llsd.h"
#include "llsingleton.h"
#include "lluuid.h"

#include <boost/container_hash/hash.hpp>
#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <deque>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class ALScriptContentsIndex;
class ALScriptTempFiles;
class ALSourceMap;
class LLChat;
class LLEventTimer;
class LLInventoryItem;
class LLMessageSystem;
class LLViewerObject;

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
    static ALScriptLanguage resolve(const LLInventoryItem* item, std::string_view content, const std::string& requested = std::string());
    // No `default` state anywhere means Lua: the same guess the legacy
    // editor made.
    static bool looksLikeLua(std::string_view content);

    // The text of an asset the storage has fetched, from the cache, cut
    // at the first NUL as the legacy readers cut it.
    static bool readAsset(const LLUUID& asset_id, LLAssetType::EType type, std::string& out);

    // --- loading ---------------------------------------------------------

    typedef std::function<void(const ALScriptLoaded&)> load_callback_t;

    // The script's text, fetched if need be. A script the agent may not
    // view comes back without text and says so. A notecard loads the same
    // way, its text alone.
    void load(const ALScriptRef& ref, load_callback_t callback);

    // --- saving and compiling ----------------------------------------------

    // A request id to send a save with, for one who must know it first.
    U64 newRequest() { return ++mNextRequest; }
    // Whether a save of the item is on its way: a recompile skips it
    // rather than land the server's text over it.
    bool saving(const ALScriptRef& ref) const;

    // Uploads and compiles. False, with why and nothing sent, where there
    // is no region, no capability or no such object; the result otherwise
    // reaches the callback and every listener.
    bool save(const ALScriptRef& ref, const std::string& text, const ALScriptSaveOptions& options, ALScriptCompileCallback callback,
              std::string& error);

    // A notecard's text uploaded in its format with the items it carried;
    // the result says it was saved, or why not, the same way.
    bool saveNotecard(const ALScriptRef& ref, const std::string& text, const std::vector<LLPointer<LLInventoryItem>>& embedded,
                      ALScriptCompileCallback callback, std::string& error, ALScriptSender sender = ALScriptSender());

    // Something's text saved, whoever sent it (ALScriptSaved), for every
    // editor to hear.
    typedef boost::signals2::signal<void(const ALScriptSaved&)> saved_signal_t;
    boost::signals2::connection onSaved(const saved_signal_t::slot_type& slot) { return mSaved.connect(slot); }

    // A script's text as it goes up again: expanded afresh from its
    // source where the preprocessor wrapped it or is on, so that its
    // includes are current, and wrapped again; or as it is. The
    // preprocessor's errors, where it found any, are the diagnostics and
    // nothing goes up -- or, `anyway`, it goes up wrapped as the
    // preprocessor made it, the source safe in the envelope, with its
    // errors said: a save of someone's work, which always goes up.
    typedef std::function<void(const ALScriptPrepared&)> prepared_callback_t;
    void prepare(const ALScriptRef& ref, const std::string& name, const LLUUID& asset_id, const std::string& text, bool lua,
                 const std::string& target, prepared_callback_t callback, bool anyway = false);

    // A script fetched, prepared, and uploaded to compile for a target:
    // what was asked, or "auto" for what it compiles for now. What the
    // compiler said reaches the callback and every listener; a script
    // that cannot go up says why in the result's error. It runs after
    // unless `running` says it was known to be stopped.
    void recompile(const ALScriptRef& ref, const std::string& target, ALScriptCompileCallback callback,
                   std::optional<bool> running = std::nullopt, ALScriptSender sender = ALScriptSender(ALScriptOrigin::Recompile));

    // The server's error strings as diagnostics.
    static std::vector<ALScriptDiagnostic> parseDiagnostics(const LLSD& errors, bool lua);

    typedef boost::signals2::signal<void(const ALScriptCompileResult&)> compiled_signal_t;
    boost::signals2::connection onCompiled(const compiled_signal_t::slot_type& slot) { return mCompiled.connect(slot); }

    // --- what RLVa allows ----------------------------------------------------

    // Why RLVa refuses a use, in its own words, or nothing where it allows
    // it, as the viewer's own windows have it: no script seen under
    // @viewscript nor notecard under @viewnote, wherever it is; and in an
    // object, nothing seen or changed where @edit or @editobj keeps the
    // build floater -- whose Contents are where an object's items are --
    // from the object, nor in a locked attachment. `object` is null for an
    // item in the inventory; `type` is the item's, or AT_NONE for what the
    // object holds as a whole. The studio, the compile queues and the
    // external editors' bridge all ask it.
    static std::string rlvRefusal(LLViewerObject* object, LLAssetType::EType type, ALScriptRlvUse use);

    // --- a script in an object -----------------------------------------------

    // Each false with why where nothing could be sent, which the caller
    // says: the workspace presents nothing of its own.
    bool setRunning(const ALScriptRef& ref, bool running, std::string& error);
    bool reset(const ALScriptRef& ref, std::string& error);
    // Stopped, then set running again once its region says it has stopped:
    // sent back to back, a stop sent again after it went astray could reach
    // the region after the start and leave the script stopped. Asked again
    // while the region says it still runs, and started after a while all
    // the same. False where the stop could not be sent.
    bool restart(const ALScriptRef& ref, std::string& error);

    // Whether a script in an object runs and what it compiles for, asked
    // of the region; every listener hears the answer. False where there
    // is nobody to ask.
    bool askRunning(const ALScriptRef& ref);
    typedef boost::signals2::signal<void(const ALScriptRunningState&)> running_signal_t;
    boost::signals2::connection onRunningState(const running_signal_t::slot_type& slot) { return mRunningState.connect(slot); }
    // The region's answer, registered for the message; the legacy live
    // editor hears it through here.
    static void processScriptRunningReply(LLMessageSystem* msg, void** data);

    // The experience a script in an object runs under, asked of its
    // region: the null one for none -- and for a script in the inventory,
    // or a region that keeps no experiences -- and nothing where the
    // region could not be asked, or did not answer. Known for the asset
    // the item holds -- asked before, or saved from here -- it is answered
    // at once: a script is put under another experience only by a save,
    // which gives it another asset.
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
    // Whether an item may be taken out of an object, as the build floater's
    // contents let one go: copied where it may be copied and given, the
    // item itself out of an object of one's own; nothing out of a locked
    // attachment, and only a copy out of any attachment, whose contents
    // the region does not keep up with. What a drag out of the studio's
    // explorer and a transfer both go by.
    static bool takeable(LLViewerObject* object, const LLInventoryItem& item);
    // What an object in world is called: an avatar's name; the selection's
    // word for it, while it is selected, which a rename there changes at
    // once; or the last the region said of it, which the object properties
    // cache keeps -- bounded, and shared with the scene explorer -- whether
    // it was selected or asked of by name. `fallback` where none has said.
    static std::string objectName(LLViewerObject* object, const std::string& fallback);
    // Whether the region a script lives in runs Lua, which is whether the
    // Lua targets are offered: the agent's region for one in the
    // inventory, or whose object is out of sight.
    static bool luaEnabled(const ALScriptRef& ref);
    typedef std::function<void(const ALScriptTransferResult&)> transfer_callback_t;
    void transfer(const LLUUID& from, const std::vector<LLUUID>& items, const LLUUID& to, bool running, transfer_callback_t done);

    // --- what an object holds ----------------------------------------------------

    typedef std::function<void(const ALScriptContents&)> contents_callback_t;
    // The scripts and notecards a prim holds, fetched from the region if
    // need be, answered once on the main thread: as not fetched where the
    // region has not answered within a while. `from_region` asks the
    // region even where the object keeps a copy: after a change the object
    // hears of only while it is selected -- a drop into it keeps the copy
    // it had -- or where a person asked.
    void listContents(const LLUUID& prim, contents_callback_t callback, bool from_region = false);
    // What every prim asked about holds, as its region last said, and
    // whether its scripts run: the one index the studio's windows, lookups
    // and searches read (alscriptcontentsindex.h).
    ALScriptContentsIndex& contentsIndex() { return *mContentsIndex; }
    // The copies of scripts written to the temp folder for an editor
    // outside, whichever window writes one (alscripttempfiles.h): named
    // alike, held rather than owned, and what a session that crashed left
    // swept at login.
    ALScriptTempFiles& tempFiles();

    // --- changing what an object holds ---------------------------------------------

    typedef std::function<void(const ALScriptCreated&)> created_callback_t;

    // A new script, in one language and from the region's template, or a
    // new notecard, in a prim's contents. False with why where nothing was
    // asked for; the answer otherwise reaches the callback on the main
    // thread.
    bool create(const LLUUID& prim, bool notecard, bool lua, const std::string& name, created_callback_t callback, std::string& error);
    // A new name for a script or notecard, in an object or in inventory.
    bool rename(const ALScriptRef& ref, const std::string& name, std::string& error);
    // A new description for one, as the legacy editor's field gives it: at
    // most what an item's description holds, and refused where the item
    // or its object may not be changed.
    bool describe(const ALScriptRef& ref, const std::string& description, std::string& error);
    // A new name for a prim -- an object's is its root prim's -- as the
    // build floater's gives one, whether or not it is selected: refused
    // where it may not be changed, or RLVa keeps it from being edited.
    bool renameObject(const LLUUID& prim, const std::string& name, std::string& error);
    // An item taken out of an object, which is for good, or an inventory
    // item put in the trash.
    bool remove(const ALScriptRef& ref, std::string& error);

    // --- what scripts say ------------------------------------------------------

    // Every debug-channel and owner-say line the viewer hears from an
    // object goes through here, and every line the agent's own objects say
    // aloud, to the agent alone, or -- `instant` -- in an IM. A run-time
    // error's lines arrive one message at a time; they are joined for a
    // moment before they are delivered.
    void ingestChat(const LLChat& chat, bool instant = false);
    // Whatever is still being joined, delivered as it is.
    void flushRuntime();
    // The last few hundred events, oldest first, for a pane opened late.
    const std::deque<ALScriptRuntimeEvent>& recentRuntime() const { return mRecent; }
    // A script's run-time errors among those, since it last compiled or
    // they were let go of: what a tab opened on it lists, where it was
    // closed as they were said.
    std::vector<ALScriptRuntimeEvent> runtimeErrorsOf(const LLUUID& prim, const LLUUID& item) const;
    void                      forgetRuntime(const LLUUID& item);

    typedef boost::signals2::signal<void(const ALScriptRuntimeEvent&)> runtime_signal_t;
    boost::signals2::connection onRuntime(const runtime_signal_t::slot_type& slot) { return mRuntime.connect(slot); }

private:
    struct Burst;
    struct ContentsListener;
    struct Transfer;
    void startTransfer(const std::shared_ptr<Transfer>& transfer);
    // What changed that came into the transfer's folder, taken where it is
    // what the transfer waits for.
    void transferArrived(const std::shared_ptr<Transfer>& transfer, const std::set<LLUUID>& changed);
    void transferEnd(const std::shared_ptr<Transfer>& transfer);
    void deliverRuntime(const Burst& burst);
    void sweepListeners();

    struct LoadRequest;
    static void onAssetLoaded(const LLUUID& asset_id, LLAssetType::EType type, void* user_data, S32 status, LLExtStat ext_status);
    // An answer handed to its caller and every listener, on the main
    // coroutine; and, where the text went up, said as saved with it.
    void        deliver(const ALScriptCompileResult& result, const ALScriptCompileCallback& callback, const std::string* text = nullptr);
    // A notecard's asset sent, and `text`, its text, said as saved.
    bool        uploadNotecard(const ALScriptRef& ref, const std::string& buffer, const std::string& text, bool carries,
                               ALScriptCompileCallback callback, std::string& error, ALScriptSender sender);
    bool        scriptMessage(const ALScriptRef& ref, const char* message, bool running, bool with_running, std::string& error);

    compiled_signal_t             mCompiled;
    saved_signal_t                mSaved;
    running_signal_t              mRunningState;
    U64                           mNextRequest = 0;
    // The saves on their way, by request, until each is answered.
    boost::unordered_flat_map<U64, ALScriptRef> mUnderway;
    std::unique_ptr<ALScriptContentsIndex> mContentsIndex;
    std::unique_ptr<ALScriptTempFiles>     mTempFiles;
    // What leaves the object list let go of by the index.
    boost::signals2::scoped_connection     mPresenceConnection;
    // Shared, so that the timer answering one the object never did can
    // tell whether it is still here.
    std::vector<std::shared_ptr<ContentsListener>> mListeners;
    std::unique_ptr<Burst>        mBurst;
    // The one look a gathered error gets, a moment after its first line;
    // there only while one is being gathered. It lets itself go once it
    // has looked, and is let go of here where the error goes first.
    LLEventTimer*                 mBurstTimer = nullptr;
    std::deque<ALScriptRuntimeEvent> mRecent;
    runtime_signal_t              mRuntime;
    // When each script's errors were last let go of, by its item.
    boost::unordered_flat_map<LLUUID, F64> mRuntimeSince;
    // The one in front is under way; the rest wait for it to end.
    std::vector<std::shared_ptr<Transfer>> mTransfers;
    // Saves waiting to hear whether a script runs, by (object, item), each
    // wait numbered so that the timer of an earlier one answers none.
    struct RunningWait
    {
        U32                                              generation = 0;
        std::vector<std::function<void(std::optional<bool>)>> told;
    };
    boost::unordered_flat_map<std::pair<LLUUID, LLUUID>, RunningWait> mRunningWaits;
    U32                                                              mRunningGeneration = 0;
    // Whether a script runs: the index's word, or the region asked and
    // waited for a while; nothing where it could not be asked or did not
    // answer.
    void awaitRunning(const ALScriptRef& ref, std::function<void(std::optional<bool>)> told);
    void answerRunning(const ALScriptRef& ref, std::optional<bool> running, U32 generation = 0);
    // Each task script's experience as last known, with the asset it was
    // known for: (object, item) to (asset, experience).
    boost::unordered_flat_map<std::pair<LLUUID, LLUUID>, std::pair<LLUUID, LLUUID>> mExperiences;
    void                                knownExperience(const ALScriptRef& ref, const LLUUID& asset, const LLUUID& experience);
    std::vector<LLUUID>                 mOwnExperiences;
    std::vector<experiences_callback_t> mOwnExperiencesWaiting;
    bool                                mOwnExperiencesAsked = false;
    bool                                mOwnExperiencesKnown = false;
};
