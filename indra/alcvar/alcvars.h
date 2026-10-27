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

#include "llcoros.h"
#include "llsingleton.h"

#include LLCOROS_MUTEX_HEADER

#include <boost/unordered/unordered_flat_map.hpp>

#include <atomic>
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
// waits for a lock gives the rest of its thread's coroutines their turn
// rather than stopping the thread.
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
    bool setSession(U32 index, const ALCVarValue& value);
    void clearSession(U32 index);
    // Replaces the Quality layer: what the feature table gives for the
    // quality level in use. With `dropUser`, the user's values of every
    // setting it names are dropped too, as choosing a level does.
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
    void apply(std::span<const U32> indices);
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

    mutable LLCoros::Mutex                     mRetiredMutex;
    std::vector<boost::shared_ptr<const void>> mRetired;
};
