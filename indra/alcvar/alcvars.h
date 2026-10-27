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

#include <atomic>
#include <memory>
#include <vector>

// The settings store. It takes the schema the generator wrote at startup
// (registerSchema), which gives every setting an index and puts every slot
// back to its compiled default; from then on the handles read their slots
// directly and write through it.
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

    // Generations, which move with every change of an effective value: the
    // setting's, its tags', and its group's and every group's above it.
    U32 generation(U32 index) const { return mGenerations[index].load(std::memory_order_acquire); }
    U64 groupGeneration(U32 group) const { return mGroupGenerations[group].load(std::memory_order_acquire); }
    U64 tagGeneration(U32 tag) const { return mTagGenerations[tag].load(std::memory_order_acquire); }

private:
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
    };

    struct Group
    {
        const ALCVarGroupDesc* desc   = nullptr;
        U32                    parent = NONE;
    };

    // Writes a value into a setting's slot, as a reader on any thread may see
    // it at any moment: whole, and never torn. The group's sequence is the
    // caller's to hold odd around it.
    void publish(U32 index, const ALCVarValue& value);
    ALCVarValue readSlot(U32 index) const;
    // The value read() and the handles see before anything has been
    // published into a string's or list's slot.
    boost::shared_ptr<const void> sharedValue(const ALCVarValue& value) const;

    const ALCVarSchema*                   mSchema = nullptr;
    std::vector<Setting>                  mSettings;
    std::vector<Group>                    mGroups;
    std::unique_ptr<std::atomic<U32>[]>   mGenerations;
    std::unique_ptr<std::atomic<U64>[]>   mGroupGenerations;
    std::unique_ptr<std::atomic<U64>[]>   mTagGenerations;
    // The LLSD values this store made for the boxed slots.
    std::vector<std::unique_ptr<LLSD>>    mBoxes;
};
