/**
 * @file alcvars.h
 * @brief The settings store: the schema's settings by index, their layers,
 *        and what reads and writes them by something other than a handle.
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

#include "alcvar.h"
#include "alcvarconvert.h"
#include "alcvarfile.h"
#include "alcvarref.h"

#include "llcoros.h"
#include "llsingleton.h"

#include LLCOROS_MUTEX_HEADER
#include LLCOROS_CONDVAR_HEADER

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Where a setting's value comes from, lowest first. The effective value is
// the top layer present, then held or clamped by its owners.
enum class ALCVarLayer : U8
{
    // The schema's.
    Default,
    // The feature table's values for the quality level in use: worked out
    // again each run, never kept.
    Quality,
    // The user's, kept in the global settings file.
    Global,
    // The user's for this account, kept in the account's file: an account
    // setting's, or an account_override setting's for this account alone.
    Account,
    // The command line, --set, a forced skin: never kept, and cleared by the
    // user's own next write.
    Session,
};

// A setting, or a group and everything below it, as a restriction names it.
struct ALCVarTarget
{
    static ALCVarTarget setting(U32 index) { return { false, index }; }
    static ALCVarTarget group(U32 index) { return { true, index }; }

    bool isGroup = false;
    U32  index   = 0;
};

struct ALCVarStats
{
    // Times the store's write lock was taken.
    U64 locks   = 0;
    // Effective values that changed.
    U64 changes = 0;
};

// The settings store. It takes the schema the generator wrote at startup
// (registerSchema), which gives every setting an index and puts every slot
// back to its compiled default; from then on the handles read their slots
// directly and write through it.
//
// Every write, from any thread, is serialized by one lock, under which the
// layers change and the effective value is worked out and published: a
// reader on any thread sees the value whole as soon as set() returns. A
// write that would change nothing returns before the lock. Its locks are
// the coroutines' own, so a write may come from any coroutine: one that
// waits for a lock -- the flusher's, while it hands a flush to the file --
// gives the rest of its thread's coroutines their turn rather than stopping
// the thread.
class ALCVars : public LLSingleton<ALCVars>
{
    LLSINGLETON(ALCVars);
    ~ALCVars() override;

public:
    static constexpr U32 NONE = U32(-1);

    // Takes the schema's groups, once. Every slot is set to its compiled
    // default, every string and list slot to its default's value, and every
    // LLSD setting's default parsed.
    void registerSchema(const ALCVarSchema& schema);
    bool registered() const { return mSchema != nullptr; }
    const ALCVarSchema& schema() const { return *mSchema; }

    U32 settingCount() const { return U32(mSettings.size()); }
    U32 groupCount() const { return U32(mGroups.size()); }
    const ALCVarDesc& desc(U32 index) const { return *mSettings[index].desc; }
    const ALCVarGroupDesc& groupDesc(U32 group) const { return *mGroups[group].desc; }
    U32 groupOf(U32 index) const { return mSettings[index].group; }
    // A group's parent, NONE for an area.
    U32 parentOf(U32 group) const { return mGroups[group].parent; }
    // The group of this name, or NONE.
    U32 findGroup(std::string_view name) const;
    // Whether the setting is in the group or a group below it.
    bool inGroup(U32 index, U32 group) const;

    // The effective value, as its slot holds it: any thread, but an LLSD
    // setting's on the main thread only.
    ALCVarValue get(U32 index) const;
    // The compiled default.
    ALCVarValue defaultValue(U32 index) const;
    // A descriptor's compiled default, before or without registration.
    static ALCVarValue defaultOf(const ALCVarDesc& desc);
    // Puts every slot of a schema back as the program starts with it: the
    // compiled defaults, and nothing published into a string, list or LLSD
    // slot. For the tests, between stores; no store may have it registered.
    static void resetSlots(const ALCVarSchema& schema);

    // ---- the user's values ---------------------------------------------

    // The user's value, from any thread: written into the setting's own
    // layer -- Global, or Account for an account setting and for an
    // account_override one the account has a value of -- and the Session
    // value cleared. A value equal to the compiled default is not kept
    // unless the Quality layer gives the setting a value of its own. False
    // when refused: of the wrong type, not finite, not one of the choices,
    // or denied by a restriction. A number past the range is clamped into
    // it.
    bool set(U32 index, const ALCVarValue& value);
    // Drops the user's value: the account's of an account_override setting
    // when it has one, otherwise the setting's own layer's. A setting whose
    // writes are denied keeps it, here and in every other write of the
    // user's layers.
    void reset(U32 index);
    // Drops the user's value of every setting in the group and below it.
    void resetGroup(U32 group);
    bool isUserSet(U32 index) const;

    // ---- the layers the user does not write ----------------------------

    // A Session value: over the user's, until the user writes the setting.
    // Validated as set() validates, its validator's verdict included.
    bool setSession(U32 index, const ALCVarValue& value);
    void clearSession(U32 index);
    // Replaces the Quality layer: what the feature table gives for the
    // quality level in use. With `dropUser`, the user's values of every
    // setting it names are dropped too, as choosing a level does.
    //
    // The first level after a file imported an LLControlGroup's XML -- or the
    // import itself, where a level is in place already -- settles what the
    // XML could not say: a value it held that the level gives is the feature
    // table's, not the user's, and goes; a setting of the file's own scope
    // the level names that the XML did not hold was at its default, which
    // LLControlGroup never saves, and the default is kept as the user's.
    void setQuality(std::span<const std::pair<U32, ALCVarValue>> values, bool dropUser);

    // The top layer the setting has a value in.
    ALCVarLayer layerOf(U32 index) const;
    // The setting's value in one layer, if it has one there.
    std::optional<ALCVarValue> layerValue(U32 index, ALCVarLayer layer) const;

    // ---- owners ----------------------------------------------------------

    // Fixes a setting's value until the owner releases it, fitted as a write
    // is: clamped into the range, refused unless it is one of the choices.
    // Several owners' holds stack: the last placed wins, and the one before
    // it shows when that is released.
    void hold(std::string_view owner, U32 index, const ALCVarValue& value);
    // Bounds a number until the owner releases it: over the layers and the
    // holds alike, and within the declared range. Several owners' bounds
    // combine, the tightest winning; where they leave nothing between them,
    // the higher minimum wins. Bounds are finite: a side with no bound is the
    // type's most.
    void clamp(std::string_view owner, U32 index, F64 lowest, F64 highest);
    // The owner's hold and clamp of the setting.
    void release(std::string_view owner, U32 index);
    // Refuses the user's writes of a setting, or of a group and everything
    // below it, until the owner allows them again.
    void denyWrite(std::string_view owner, ALCVarTarget target);
    void allowWrite(std::string_view owner, ALCVarTarget target);
    // Hides a setting or a group from the settings editor.
    void hide(std::string_view owner, ALCVarTarget target);
    void unhide(std::string_view owner, ALCVarTarget target);
    // Everything the owner holds, clamps and restricts.
    void releaseAll(std::string_view owner);

    bool isHeld(U32 index) const;
    bool isWriteDenied(U32 index) const;
    // Hidden by its declaration or by an owner.
    bool isHidden(U32 index) const;
    // The owners holding, clamping or restricting the setting.
    std::vector<std::string> ownersOf(U32 index) const;

    // Fits a value to its setting as a write does: true with the value
    // clamped into the range, false when it is refused.
    bool validate(U32 index, ALCVarValue& value) const;

    // ---- listeners --------------------------------------------------------

    // Listeners are the main thread's: connected, told and disconnected
    // there. A change made on the main thread outside a batch is told before
    // set() returns; one made off it is queued, coalesced by setting, and
    // told at the next pump. A listener is given the new value and the last
    // one listeners were given, and is told only of a real change, after the
    // slot holds the new value. A listener may write settings: what that
    // changes is told in the same pass, which asserts past MAX_PASS_ROUNDS
    // rounds of changes feeding changes.
    ALCVarConnection listen(U32 index, ALCVarListener listener);
    // Told of a change anywhere in the group or below it.
    ALCVarConnection listenGroup(U32 group, ALCVarGroupListener listener);
    void disconnect(ALCVarConnection::Kind kind, U32 index, U64 id);
    bool connected(ALCVarConnection::Kind kind, U32 index, U64 id) const;

    static constexpr U32 MAX_PASS_ROUNDS = 16;

    // Writes on the main thread whose listeners are told of nothing until the
    // outermost batch ends, then of each setting that changed once, in one
    // pass: a preset, a Look, a quality level, Preferences' OK and Cancel.
    // Its end asserts as a write's does when that pass keeps feeding itself,
    // except while an exception unwinds it: then its pass waits for the next
    // one. A batch that outlives its store ends nothing.
    class Batch
    {
    public:
        Batch();
        ~Batch() noexcept(false);
        Batch(const Batch&)            = delete;
        Batch& operator=(const Batch&) = delete;

    private:
        U64 mStore     = 0;
        int mUnwinding = 0;
    };

    // On the main thread, once a frame: makes the writes posted to it for a
    // validator of its own, and tells listeners of what changed off it since
    // the last pump.
    void pump();

    // ---- hooks ------------------------------------------------------------

    // A validator a declaration names by its validate field: may adjust the
    // value it is given, and refuses it with false.
    using Validator = std::function<bool(const ALCVarDesc& desc, ALCVarValue& value)>;

    enum class HookThread : U8
    {
        Any,
        // The rule reads the main thread's state: a write from another
        // thread is posted to the main thread and validated there.
        Main,
    };

    // Registered by the code that owns the rule, at init, before any thread
    // but the main one writes. Every write of a Main-validated setting from
    // another thread -- set, reset, a Session value set or cleared -- is
    // posted, unchanged ones included, so the main thread makes them in the
    // order they were made.
    void registerValidator(std::string_view name, Validator validator, HookThread thread = HookThread::Any);
    // A migration a declaration's migrate names by its fn, registered as a
    // validator is.
    void registerMigration(std::string_view name, ALCVarMigration migration);
    const ALCVarMigration* migration(std::string_view name) const;
    // Asserts, once startup is done, that every hook the schema names has
    // been registered.
    void checkHooks() const;

    // ---- names ------------------------------------------------------------

    // The setting of a name or an old name, from any thread: made once at
    // registration over the descriptors' names and aliases and never changed
    // after, so read with no lock. Empty where nothing has the name.
    ALCVarRef find(std::string_view name) const;

    // ---- files ------------------------------------------------------------

    // Opens the global settings file and takes its rows into the Global
    // layer, each converted into its setting as it is declared now, and
    // waits. Rows a rename, a conversion or a scope move touches are
    // written back; rows of settings this build does not know, and those
    // that do not convert, are left as they were. False when the file is
    // not kept: it cannot be opened, is another kind of file, could be used
    // only in memory this session, or could not be read whole -- then it is
    // left as it is and the store runs on what it holds in memory. A file
    // that may only be read is read, and kept, and nothing is written to
    // it; options.onFailure is told so.
    //
    // From then on, each change of a user value is written to its file in
    // one transaction with every other since the last: QUIET after the last
    // change, at most LONGEST after the first, or at flushNow(). A slider
    // dragged through a hundred values writes one row once. A transaction
    // the file's writer drops is tried again with the next.
    bool openGlobal(const std::string& path, const ALCVarFileOptions& options = {});
    // The account's file, at each login attempt: its rows replace the
    // Account layer under one write, every group's sequence held odd until
    // the last slot is written, so no thread sees a default between two
    // accounts' values, and every setting whose value moved is told once.
    // What was written to account settings before it is laid over its rows,
    // and kept only if the login succeeds. False as openGlobal is; then an
    // account open before it is closed, as closeAccount() closes it.
    bool openAccount(const std::string& path, const ALCVarFileOptions& options = {});
    // The login succeeded: what was written to account settings since the
    // last login attempt is this account's, and written.
    void confirmAccount();
    // Logout, or a login that failed: the account's values go, and what was
    // written before a login that never came is dropped.
    void closeAccount();
    bool accountOpen() const;

    // An account_override setting's "This account only": the value the
    // global layer gives moved into this account's own; and "All
    // accounts": the account's own dropped.
    void moveToAccount(U32 index);
    void moveToGlobal(U32 index);

    // Writes every change now and waits for it, within the files' bounded
    // wait, both files at once: at login, logout, after Preferences' OK,
    // before the steps most likely to crash.
    void flushNow();
    // Writes every change, waits for it within the files' bounded wait, and
    // closes them. Nothing else is written at exit.
    void close();

    static constexpr std::chrono::milliseconds QUIET{ 250 };
    static constexpr std::chrono::milliseconds LONGEST{ 1000 };
    // For the tests: shorter waits.
    void setFlushDelays(std::chrono::milliseconds quiet, std::chrono::milliseconds longest);
    // Which run of the viewer made a change, as its history says.
    S64 session() const { return mSession; }

    // ---- frames ---------------------------------------------------------

    // On the main thread, at the end of each frame: frees the strings and
    // lists that writes replaced during it, which a view taken in the frame
    // may still be looking at.
    void endFrame();
    size_t retiredCount() const;

    // Generations, which move with every change of an effective value: the
    // setting's, its tags', and its group's and every group's above it.
    U32 generation(U32 index) const { return mGenerations[index].load(std::memory_order_acquire); }
    U64 groupGeneration(U32 group) const { return mGroupGenerations[group].load(std::memory_order_acquire); }
    U64 tagGeneration(U32 tag) const { return mTagGenerations[tag].load(std::memory_order_acquire); }

    ALCVarStats stats() const;

private:
    // What set() can tell without the lock, kept for each setting under it.
    enum : U8
    {
        // The layer the user's write goes into has a value.
        FLAG_USER    = 1 << 0,
        FLAG_QUALITY = 1 << 1,
        FLAG_SESSION = 1 << 2,
        // Held or clamped by an owner.
        FLAG_OWNED   = 1 << 3,
        FLAG_DENIED  = 1 << 4,
        FLAG_HIDDEN  = 1 << 5,
    };

    struct Setting
    {
        const ALCVarDesc*      desc = nullptr;
        const ALCVarGroupDesc* groupDesc = nullptr;
        U32                    group = 0;
        ALCVarStorage          storage = ALCVarStorage::Byte;
        // Its slot in its group's array of its storage.
        void*                  slot = nullptr;
        // The schema's tags it carries, by index.
        std::vector<U32>       tags;
        ALCVarValue            def;
        // Its validator, among those registered.
        U32                    validator = NONE;
    };

    // A change of an effective value, with the value before it.
    struct Change
    {
        U32         index = 0;
        ALCVarValue old;
    };

    template <typename F>
    struct Listeners
    {
        struct Entry
        {
            U64  id    = 0;
            F    fn;
            bool alive = true;
        };
        // Each entry where it is for as long as a pass may be calling it.
        std::vector<std::unique_ptr<Entry>> entries;
    };

    struct ValidatorHook
    {
        std::string name;
        Validator   fn;
        HookThread  thread = HookThread::Any;
    };

    struct Group
    {
        const ALCVarGroupDesc* desc   = nullptr;
        U32                    parent = NONE;
    };

    struct Hold
    {
        std::string owner;
        ALCVarValue value;
    };

    struct Clamp
    {
        std::string owner;
        F64         lowest  = 0.0;
        F64         highest = 0.0;
    };

    using Layer  = boost::unordered_flat_map<U32, ALCVarValue>;
    using Owners = std::vector<std::string>;

    LLCoros::LockType lockWrites() const;

    Layer&       layer(ALCVarLayer which) { return mLayers[size_t(which) - 1]; }
    const Layer& layer(ALCVarLayer which) const { return mLayers[size_t(which) - 1]; }
    // The user layers a setting reads, the top first: Account then Global
    // for an account_override setting, its scope's alone otherwise.
    bool readsAccount(U32 index) const;
    bool readsGlobal(U32 index) const;
    // The layer the user's write of the setting goes into.
    ALCVarLayer userLayer(U32 index) const;

    // The value the layers and owners give, under the write lock.
    ALCVarValue effective(U32 index) const;
    // Works out these settings' effective values again, under the write
    // lock, and publishes those that changed, each touched group's sequence
    // held odd until the last is written, so that its read() sees all of the
    // change or none.
    // The changes of settings anyone listens to are added to `changes`.
    void apply(std::span<const U32> indices, std::vector<Change>* changes);
    // After the write lock is let go, on the main thread: tells listeners of
    // the changes. Changes made on another thread are queued by apply(),
    // under the lock, so the queue holds them in the order they were made.
    void noteChanges(std::vector<Change>& changes);
    void queueChanges(std::vector<Change>& changes);
    void addPending(Change&& change);
    // The changes written off the main thread, into this pass.
    void drainQueued();
    void deliver();
    // Removes the listeners disconnected during a pass.
    void sweep();
    bool listened(U32 index) const { return mListened[index].load(std::memory_order_relaxed) != 0; }
    void updateListened();
    void beginBatch();
    // `deliver`: tell the pass now, or leave it for the next.
    void endBatch(bool deliver);
    // Which store this is: a batch begun on one is not ended on another.
    U64 serial() const { return mSerial; }
    // The validator's verdict on a write, and true for a setting with none.
    bool runValidator(U32 index, ALCVarValue& value);
    // A write the main thread is to make, from another thread, for a
    // setting whose validator is the main thread's.
    struct PostedWrite
    {
        enum class Kind : U8
        {
            Set,
            Reset,
            Session,
            ClearSession,
        };
        U32         index = 0;
        Kind        kind  = Kind::Set;
        ALCVarValue value;
    };
    bool postsToMain(U32 index) const;
    void post(U32 index, PostedWrite::Kind kind, ALCVarValue value = {});
    void updateFlags(U32 index);
    void updateAllFlags();
    // A setting's write sequence held odd while a write under the lock
    // changes its flags or its slot: what set()'s check before the lock
    // reads around them.
    void beginSettingWrite(U32 index);
    void endSettingWrite(U32 index);
    // Whether the set() of this value would leave everything as it is, as
    // the flags given and the slot say.
    bool unchanged(U32 index, const ALCVarValue& value, U8 flags) const;
    bool slotHolds(U32 index, const ALCVarValue& value) const;
    bool restricted(const boost::unordered_flat_map<U64, Owners>& restrictions, U32 index) const;
    static U64 keyOf(ALCVarTarget target) { return (U64(target.isGroup) << 32) | target.index; }
    void restrict(boost::unordered_flat_map<U64, Owners>& restrictions, std::string_view owner,
                  ALCVarTarget target, bool add);

    // A user layer's value of a setting set, or removed, and marked to be
    // written to its file: under the write lock.
    void putUser(ALCVarLayer which, U32 index, std::optional<ALCVarValue> value);
    void markDirty(ALCVarLayer which, U32 index, std::optional<ALCVarValue> before,
                   std::optional<ALCVarValue> after);

    // A user value's change still to be written: its oldest old value and
    // newest new one, an LLSD setting's as its text (ALCVarFile::persisted).
    struct Dirty
    {
        std::optional<ALCVarValue> before;
        std::optional<ALCVarValue> after;
        S64                        modified = 0;
    };

    // A file's flushes its writer has not finished, shared with each one's
    // completion, which the writer may tell after the store has let the file
    // go: what a dropped flush carried, for the next to write.
    struct Posted
    {
        LLCoros::Mutex                        mutex;
        U64                                   flushes = 0;
        // Each setting's last flush posted.
        boost::unordered_flat_map<U32, U64>   last;
        // The changes of dropped flushes that nothing posted since replaces.
        boost::unordered_flat_map<U32, Dirty> dropped;
    };

    // One file's: the file, what is to be written to it, and every name it
    // has a row of or is being given one, tombstones included.
    struct Scope
    {
        std::shared_ptr<ALCVarFile>            file;
        // Open only for reading: what is dirty stays in memory.
        bool                                   readOnly = false;
        std::shared_ptr<Posted>                posted;
        boost::unordered_flat_map<U32, Dirty>  dirty;
        boost::unordered_flat_set<std::string> rows;
        // The stamp of an import a quality level has yet to settle, 0 for
        // none, and the settings whose user value is still the import's.
        S64                                    importPending = 0;
        boost::unordered_flat_set<U32>         imported;
    };

    // A file's rows, resolved into the settings they are now.
    struct Loaded
    {
        Layer                                  values;
        // Rows to write back: renamed, brought forward, or moved in; the
        // rows a migrate replaced, kept; and the history of each.
        std::vector<ALCVarRow>                 rewrites;
        std::vector<ALCVarRow>                 prior;
        std::vector<ALCVarHistory>             history;
        boost::unordered_flat_set<std::string> rows;
        // Values of settings of the other scope, with when they were written.
        std::vector<std::pair<U32, std::pair<ALCVarValue, S64>>> others;
        // The settings whose value is the row an import wrote at `stamp`.
        boost::unordered_flat_set<U32>         imported;
    };
    Loaded load(const std::vector<ALCVarRow>& rows, const std::vector<ALCVarRow>& prior, ALCVarLayer which,
                S64 stamp = 0) const;
    // Under the write lock: settles a scope's pending import against the
    // Quality layer, adding what it changes to `touched`.
    void settleImport(ALCVarLayer which, bool dropUser, std::vector<U32>& touched);
    // Imports an LLControlGroup settings file into a file that has imported
    // none.
    void importXml(ALCVarFile& file, const std::string& xml, ALCVarLayer which);
    // An import's stamp the file's meta records as pending, or 0.
    static S64 importPending(const boost::unordered_flat_map<std::string, std::string>& meta);
    ALCVarRow rowOf(U32 index, const std::optional<ALCVarValue>& value, S64 modified) const;
    // A file just opened, checked and read: kept, or closed and false. An
    // open account is closed with it.
    bool usableFile(ALCVarFile& file, const ALCVarFileOptions& options, ALCVarLayer which);
    // Under the persistence lock: the scope's file becomes this one.
    void attach(Scope& scope, std::shared_ptr<ALCVarFile> file);
    // Under the persistence lock: posts what is dirty, with what a dropped
    // flush left.
    void writeDirty(Scope& scope);
    // Whether a change of the setting is waiting to be written to the
    // scope's file.
    bool pendingIn(const Scope& scope, U32 index) const;
    class Flusher;
    // Under the persistence lock: the flusher started, for whichever file
    // opens first.
    void startFlusher();
    void flusherLoop();
    void stopFlusher();

    // Writes a value into a setting's slot, as a reader on any thread may see
    // it at any moment: whole, and never torn. The group's sequence is the
    // caller's to hold odd around it.
    void publish(U32 index, const ALCVarValue& value);
    ALCVarValue readSlot(U32 index) const;
    // Keeps a string or list a write replaced until the main thread's frame
    // ends.
    void retire(boost::shared_ptr<const void> value);
    // Moves the generations of a setting whose effective value changed.
    void bump(U32 index);

    const U64                             mSerial;
    const ALCVarSchema*                   mSchema = nullptr;
    std::vector<Setting>                  mSettings;
    std::vector<Group>                    mGroups;
    std::unique_ptr<std::atomic<U32>[]>   mGenerations;
    std::unique_ptr<std::atomic<U64>[]>   mGroupGenerations;
    std::unique_ptr<std::atomic<U64>[]>   mTagGenerations;
    std::unique_ptr<std::atomic<U8>[]>    mFlags;
    std::unique_ptr<std::atomic<U32>[]>   mWriteSeq;
    // The LLSD values this store made for the boxed slots.
    std::vector<std::unique_ptr<LLSD>>    mBoxes;

    mutable LLCoros::Mutex                mWriteMutex;
    mutable std::atomic<U64>              mLocks{ 0 };
    std::atomic<U64>                      mChanges{ 0 };
    // Under the write lock: Quality, Global, Account and Session.
    Layer                                 mLayers[4];
    boost::unordered_flat_map<U32, std::vector<Hold>>  mHolds;
    boost::unordered_flat_map<U32, std::vector<Clamp>> mClamps;
    boost::unordered_flat_map<U64, Owners>             mDenied;
    boost::unordered_flat_map<U64, Owners>             mHidden;

    // The main thread's: listeners by setting and by group, null for none,
    // what is still to be told, and whether a pass or a batch is open.
    std::vector<std::unique_ptr<Listeners<ALCVarListener>>>      mListeners;
    std::vector<std::unique_ptr<Listeners<ALCVarGroupListener>>> mGroupListeners;
    // Whether anyone listens to a setting, or to a group it is in: what a
    // write off the main thread reads to know whether to queue its change.
    std::unique_ptr<std::atomic<U8>[]>                           mListened;
    std::vector<Change>                                          mPending;
    boost::unordered_flat_map<U32, size_t>                       mPendingAt;
    // The value each listened setting's listeners were last given: what a
    // change is told from, so that one told already, or made again before it
    // was told, is not told twice.
    boost::unordered_flat_map<U32, ALCVarValue>                  mTold;
    U32                                                          mBatchDepth = 0;
    bool                                                         mInPass     = false;
    bool                                                         mSwept      = true;
    bool                                                         mSweeping   = false;
    // The store is going: its listeners are being let go, and what they own
    // disconnects from nothing.
    bool                                                         mTearingDown = false;
    std::vector<ValidatorHook>                                   mValidators;
    std::vector<std::pair<std::string, ALCVarMigration>>         mMigrations;

    // Every name and old name, to its setting.
    struct Named
    {
        U32                index = 0;
        const ALCVarAlias* alias = nullptr;
        // The alias's place among all of them, for its use to be said once.
        U32                ordinal = 0;
    };
    boost::unordered_flat_map<std::string_view, Named> mNames;
    std::unique_ptr<std::atomic<U8>[]>                 mAliasSaid;

    // Changes made off the main thread, and writes posted to it.
    LLCoros::Mutex                               mQueueMutex;
    std::vector<Change>                          mQueued;
    boost::unordered_flat_map<U32, size_t>       mQueuedAt;
    std::vector<PostedWrite>                     mPosted;

    // Under the write lock: whether an account's file is open, whether its
    // login succeeded, and the account writes kept for a login to come; and
    // the global file's values of settings that are now an account's, for
    // each account to take once.
    bool                                                         mAccountOpen      = false;
    // A quality level has been applied this session.
    bool                                                         mQualityApplied   = false;
    bool                                                         mAccountConfirmed = false;
    boost::unordered_flat_set<U32>                               mAccountPending;
    boost::unordered_flat_map<U32, std::pair<ALCVarValue, S64>>  mGlobalForAccounts;

    // Under the persistence lock, which is taken inside the write lock and
    // never the other way.
    LLCoros::Mutex                             mPersistMutex;
    LLCoros::ConditionVariable                 mFlushWake;
    Scope                                      mGlobal;
    Scope                                      mAccount;
    // A change made since the last flush: what the flusher waits for.
    // What a dropped flush left waits for the next one, not a flush of its
    // own, which would drop it again as fast as the writer can.
    bool                                       mUnflushed = false;
    std::chrono::steady_clock::time_point      mFirstDirty;
    std::chrono::steady_clock::time_point      mLastDirty;
    std::chrono::milliseconds                  mQuiet   = QUIET;
    std::chrono::milliseconds                  mLongest = LONGEST;
    bool                                       mStopping = false;
    std::unique_ptr<Flusher>                   mFlusher;
    // Batches open on the main thread: a batch's writes go in one
    // transaction, so none is flushed while one is open.
    std::atomic<U32>                           mBatchesOpen{ 0 };
    S64                                        mSession = 0;

    mutable LLCoros::Mutex                     mRetiredMutex;
    std::vector<boost::shared_ptr<const void>> mRetired;
};
