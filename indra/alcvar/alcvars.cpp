/**
 * @file alcvars.cpp
 * @brief The settings store.
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

#include "linden_common.h"

#include "alcvars.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/smart_ptr/make_shared.hpp>

namespace
{
    template <size_t N>
    void writeWords(ALCVarSeqSlot<N>& slot, const U64* words)
    {
        const U64 sequence = ALCVarAtomic::loadRelaxed(&slot.sequence);
        ALCVarAtomic::storeRelaxed(&slot.sequence, sequence + 1);
        ALCVarAtomic::fenceRelease();
        for (size_t i = 0; i < N; ++i)
        {
            ALCVarAtomic::storeRelaxed(&slot.words[i], words[i]);
        }
        ALCVarAtomic::storeRelease(&slot.sequence, sequence + 2);
    }

    template <typename T>
    ALCVarValue fromWords(const U64* words)
    {
        return ALCVarValue(ALCVarTraits<T>::decode(words));
    }

    template <typename T>
    std::array<U64, 3> toWords(const ALCVarValue& value)
    {
        const auto         words = ALCVarTraits<T>::encode(value.get<T>());
        std::array<U64, 3> out{};
        std::copy(words.begin(), words.end(), out.begin());
        return out;
    }

    // A fixed-size value from the words its slot or its default holds.
    ALCVarValue decodeWords(ALCVarType type, const U64* words)
    {
        switch (type)
        {
            case ALCVarType::Bool:   return ALCVarValue(ALCVarTraits<bool>::decode(U8(words[0])));
            case ALCVarType::S32:    return ALCVarValue(ALCVarTraits<S32>::decode(U32(words[0])));
            case ALCVarType::U32:    return ALCVarValue(ALCVarTraits<U32>::decode(U32(words[0])));
            case ALCVarType::F32:    return ALCVarValue(ALCVarTraits<F32>::decode(U32(words[0])));
            case ALCVarType::S64:    return ALCVarValue(ALCVarTraits<S64>::decode(words[0]));
            case ALCVarType::F64:    return ALCVarValue(ALCVarTraits<F64>::decode(words[0]));
            case ALCVarType::Vec2:   return ALCVarValue(ALCVarTraits<LLVector2>::decode(words[0]));
            case ALCVarType::UUID:   return fromWords<LLUUID>(words);
            case ALCVarType::Vec3:   return fromWords<LLVector3>(words);
            case ALCVarType::Quat:   return fromWords<LLQuaternion>(words);
            case ALCVarType::Rect:   return fromWords<LLRect>(words);
            case ALCVarType::Color3: return fromWords<LLColor3>(words);
            case ALCVarType::Color4: return fromWords<LLColor4>(words);
            case ALCVarType::Vec3d:  return fromWords<LLVector3d>(words);
            default:                 return {};
        }
    }

    std::array<U64, 3> encodeWords(const ALCVarValue& value)
    {
        switch (value.type())
        {
            case ALCVarType::Bool:   return { ALCVarTraits<bool>::encode(value.get<bool>()), 0, 0 };
            case ALCVarType::S32:    return { ALCVarTraits<S32>::encode(value.get<S32>()), 0, 0 };
            case ALCVarType::U32:    return { ALCVarTraits<U32>::encode(value.get<U32>()), 0, 0 };
            case ALCVarType::F32:    return { ALCVarTraits<F32>::encode(value.get<F32>()), 0, 0 };
            case ALCVarType::S64:    return { ALCVarTraits<S64>::encode(value.get<S64>()), 0, 0 };
            case ALCVarType::F64:    return { ALCVarTraits<F64>::encode(value.get<F64>()), 0, 0 };
            case ALCVarType::Vec2:   return { ALCVarTraits<LLVector2>::encode(value.get<LLVector2>()), 0, 0 };
            case ALCVarType::UUID:   return toWords<LLUUID>(value);
            case ALCVarType::Vec3:   return toWords<LLVector3>(value);
            case ALCVarType::Quat:   return toWords<LLQuaternion>(value);
            case ALCVarType::Rect:   return toWords<LLRect>(value);
            case ALCVarType::Color3: return toWords<LLColor3>(value);
            case ALCVarType::Color4: return toWords<LLColor4>(value);
            case ALCVarType::Vec3d:  return toWords<LLVector3d>(value);
            default:                 return {};
        }
    }

    // A string's or list's value as its slot shares it.
    boost::shared_ptr<const void> makeShared(const ALCVarValue& value)
    {
        switch (value.type())
        {
            case ALCVarType::String:
                return boost::make_shared<const std::string>(value.get<std::string>());
            case ALCVarType::UUIDList:
                return boost::make_shared<const std::vector<LLUUID>>(value.get<std::vector<LLUUID>>());
            case ALCVarType::StringList:
                return boost::make_shared<const std::vector<std::string>>(value.get<std::vector<std::string>>());
            case ALCVarType::Vec2List:
                return boost::make_shared<const std::vector<LLVector2>>(value.get<std::vector<LLVector2>>());
            default:
                return {};
        }
    }

    ALCVarValue fromShared(ALCVarType type, const void* value)
    {
        switch (type)
        {
            case ALCVarType::String:
                return ALCVarValue(*static_cast<const std::string*>(value));
            case ALCVarType::UUIDList:
                return ALCVarValue(*static_cast<const std::vector<LLUUID>*>(value));
            case ALCVarType::StringList:
                return ALCVarValue(*static_cast<const std::vector<std::string>*>(value));
            case ALCVarType::Vec2List:
                return ALCVarValue(*static_cast<const std::vector<LLVector2>*>(value));
            default:
                return {};
        }
    }

    // The group's sequence held odd while its slots change, so that its
    // read() takes the values from before every change or after them all.
    void beginGroupWrite(ALCVarGroupState& state)
    {
        const U32 sequence = ALCVarAtomic::loadRelaxed(&state.sequence);
        ALCVarAtomic::storeRelaxed(&state.sequence, sequence + 1);
        ALCVarAtomic::fenceRelease();
    }

    void endGroupWrite(ALCVarGroupState& state)
    {
        ALCVarAtomic::storeRelease(&state.sequence, ALCVarAtomic::loadRelaxed(&state.sequence) + 1);
    }
}

ALCVars::ALCVars() = default;

ALCVars::~ALCVars()
{
    // The LLSD the boxed slots point to goes with the store; a read after
    // it reads undefined LLSD rather than freed memory.
    for (const Setting& setting : mSettings)
    {
        if (setting.storage == ALCVarStorage::Boxed)
        {
            static_cast<ALCVarBoxedSlot*>(setting.slot)->value = nullptr;
        }
    }
    for (const Group& group : mGroups)
    {
        ALCVarAtomic::storeRelease(&group.desc->state->registered, U32(0));
    }
}

ALCVarValue ALCVars::defaultOf(const ALCVarDesc& desc)
{
    switch (ALCVarTypes::storageOf(desc.type))
    {
        case ALCVarStorage::Shared:
        case ALCVarStorage::Boxed:
            if (desc.type == ALCVarType::String)
            {
                return ALCVarValue(desc.def.text);
            }
            if (!desc.def.text.empty())
            {
                if (auto value = ALCVarValues::fromJson(desc.type, desc.def.text))
                {
                    return std::move(*value);
                }
                LL_WARNS("ALCVars") << desc.name << ": its default does not parse: " << desc.def.text << LL_ENDL;
            }
            return ALCVarValue::zeroOf(desc.type);
        default:
            return decodeWords(desc.type, desc.def.words);
    }
}

void ALCVars::registerSchema(const ALCVarSchema& schema)
{
    llassert_always_msg(!mSchema, "ALCVars takes one schema");
    mSchema = &schema;

    boost::unordered_flat_map<std::string_view, U32> groups;
    U32 settings = 0;
    for (const ALCVarGroupDesc* desc : schema.groups)
    {
        Group group;
        group.desc = desc;
        if (!desc->parent.empty())
        {
            const auto parent = groups.find(desc->parent);
            llassert_always_msg(parent != groups.end(), "a group's parent comes before it");
            group.parent = parent->second;
        }
        groups.emplace(desc->name, U32(mGroups.size()));
        mGroups.push_back(group);
        settings += U32(desc->settings.size());
    }

    boost::unordered_flat_map<std::string_view, U32> tags;
    for (U32 i = 0; i < U32(schema.tags.size()); ++i)
    {
        tags.emplace(schema.tags[i], i);
    }

    mSettings.reserve(settings);
    for (U32 g = 0; g < U32(mGroups.size()); ++g)
    {
        const ALCVarGroupDesc& group = *mGroups[g].desc;
        ALCVarAtomic::storeRelaxed(&group.state->first, U32(mSettings.size()));
        ALCVarAtomic::storeRelaxed(&group.state->index, g);
        for (const ALCVarDesc& desc : group.settings)
        {
            Setting setting;
            setting.desc      = &desc;
            setting.groupDesc = &group;
            setting.group     = g;
            setting.storage   = ALCVarTypes::storageOf(desc.type);
            switch (setting.storage)
            {
                case ALCVarStorage::Byte:   setting.slot = &group.bytes[desc.slot]; break;
                case ALCVarStorage::Word:   setting.slot = &group.words[desc.slot]; break;
                case ALCVarStorage::DWord:  setting.slot = &group.dwords[desc.slot]; break;
                case ALCVarStorage::Pair:   setting.slot = &group.pairs[desc.slot]; break;
                case ALCVarStorage::Triple: setting.slot = &group.triples[desc.slot]; break;
                case ALCVarStorage::Shared: setting.slot = &group.shared[desc.slot]; break;
                case ALCVarStorage::Boxed:  setting.slot = &group.boxed[desc.slot]; break;
            }
            for (const std::string_view tag : desc.tags)
            {
                setting.tags.push_back(tags.at(tag));
            }
            mSettings.push_back(std::move(setting));
        }
        ALCVarAtomic::storeRelease(&group.state->registered, U32(1));
    }

    mGenerations.reset(new std::atomic<U32>[mSettings.size()]());
    mGroupGenerations.reset(new std::atomic<U64>[mGroups.size()]());
    mTagGenerations.reset(new std::atomic<U64>[std::max<size_t>(schema.tags.size(), 1)]());

    // Every slot back to its compiled default: a store made again over the
    // same schema, as each test makes one, starts where a new process does.
    for (U32 g = 0; g < U32(mGroups.size()); ++g)
    {
        beginGroupWrite(*mGroups[g].desc->state);
    }
    for (U32 i = 0; i < U32(mSettings.size()); ++i)
    {
        Setting& setting = mSettings[i];
        if (setting.storage == ALCVarStorage::Boxed)
        {
            mBoxes.push_back(std::make_unique<LLSD>(defaultOf(*setting.desc).get<LLSD>()));
            static_cast<ALCVarBoxedSlot*>(setting.slot)->value = mBoxes.back().get();
            continue;
        }
        publish(i, defaultOf(*setting.desc));
    }
    for (U32 g = 0; g < U32(mGroups.size()); ++g)
    {
        endGroupWrite(*mGroups[g].desc->state);
    }
}

void ALCVars::resetSlots(const ALCVarSchema& schema)
{
    for (const ALCVarGroupDesc* group : schema.groups)
    {
        llassert(!ALCVarAtomic::loadRelaxed(&group->state->registered));
        for (const ALCVarDesc& desc : group->settings)
        {
            const auto words = desc.def.words;
            switch (ALCVarTypes::storageOf(desc.type))
            {
                case ALCVarStorage::Byte:   group->bytes[desc.slot] = U8(words[0]); break;
                case ALCVarStorage::Word:   group->words[desc.slot] = U32(words[0]); break;
                case ALCVarStorage::DWord:  group->dwords[desc.slot] = words[0]; break;
                case ALCVarStorage::Pair:   writeWords(group->pairs[desc.slot], words); break;
                case ALCVarStorage::Triple: writeWords(group->triples[desc.slot], words); break;
                case ALCVarStorage::Shared:
                    group->shared[desc.slot].current = nullptr;
                    group->shared[desc.slot].value.store(nullptr);
                    break;
                case ALCVarStorage::Boxed:  group->boxed[desc.slot].value = nullptr; break;
            }
        }
    }
}

U32 ALCVars::findGroup(std::string_view name) const
{
    for (U32 g = 0; g < U32(mGroups.size()); ++g)
    {
        if (mGroups[g].desc->name == name)
        {
            return g;
        }
    }
    return NONE;
}

void ALCVars::publish(U32 index, const ALCVarValue& value)
{
    Setting& setting = mSettings[index];
    llassert(value.type() == setting.desc->type);
    switch (setting.storage)
    {
        case ALCVarStorage::Byte:
            ALCVarAtomic::storeRelaxed(static_cast<U8*>(setting.slot), U8(encodeWords(value)[0]));
            break;
        case ALCVarStorage::Word:
            ALCVarAtomic::storeRelaxed(static_cast<U32*>(setting.slot), U32(encodeWords(value)[0]));
            break;
        case ALCVarStorage::DWord:
            ALCVarAtomic::storeRelaxed(static_cast<U64*>(setting.slot), encodeWords(value)[0]);
            break;
        case ALCVarStorage::Pair:
            writeWords(*static_cast<ALCVarSeqSlot<2>*>(setting.slot), encodeWords(value).data());
            break;
        case ALCVarStorage::Triple:
            writeWords(*static_cast<ALCVarSeqSlot<3>*>(setting.slot), encodeWords(value).data());
            break;
        case ALCVarStorage::Shared:
        {
            auto& slot   = *static_cast<ALCVarSharedSlot*>(setting.slot);
            auto  shared = makeShared(value);
            ALCVarAtomic::storeRelease(&slot.current, shared.get());
            slot.value.store(std::move(shared));
            break;
        }
        case ALCVarStorage::Boxed:
            llassert(on_main_thread());
            *static_cast<ALCVarBoxedSlot*>(setting.slot)->value = value.get<LLSD>();
            break;
    }
}

ALCVarValue ALCVars::readSlot(U32 index) const
{
    const Setting& setting = mSettings[index];
    switch (setting.storage)
    {
        case ALCVarStorage::Byte:
            return ALCVarValue(ALCVarAtomic::loadRelaxed(static_cast<const U8*>(setting.slot)) != 0);
        case ALCVarStorage::Word:
        {
            const U64 word = ALCVarAtomic::loadRelaxed(static_cast<const U32*>(setting.slot));
            return decodeWords(setting.desc->type, &word);
        }
        case ALCVarStorage::DWord:
        {
            const U64 word = ALCVarAtomic::loadRelaxed(static_cast<const U64*>(setting.slot));
            return decodeWords(setting.desc->type, &word);
        }
        case ALCVarStorage::Pair:
        {
            U64 words[3] = {};
            ALCVarDetail::readWords(*static_cast<const ALCVarSeqSlot<2>*>(setting.slot), words);
            return decodeWords(setting.desc->type, words);
        }
        case ALCVarStorage::Triple:
        {
            U64 words[3];
            ALCVarDetail::readWords(*static_cast<const ALCVarSeqSlot<3>*>(setting.slot), words);
            return decodeWords(setting.desc->type, words);
        }
        case ALCVarStorage::Shared:
        {
            const auto value = static_cast<const ALCVarSharedSlot*>(setting.slot)->value.load();
            return value ? fromShared(setting.desc->type, value.get()) : defaultOf(*setting.desc);
        }
        case ALCVarStorage::Boxed:
        {
            llassert(on_main_thread());
            const LLSD* value = static_cast<const ALCVarBoxedSlot*>(setting.slot)->value;
            return ALCVarValue(value ? *value : LLSD());
        }
    }
    return {};
}

ALCVarValue ALCVars::get(U32 index) const
{
    return readSlot(index);
}

ALCVarValue ALCVars::defaultValue(U32 index) const
{
    return defaultOf(*mSettings[index].desc);
}

boost::shared_ptr<const void> ALCVars::sharedValue(const ALCVarValue& value) const
{
    return makeShared(value);
}

namespace ALCVarAccess
{
    namespace
    {
        // The store there is, never one made here: a store asked for on a
        // worker is made on the main thread, which may be waiting for that
        // worker. Null before it exists and once it has gone.
        ALCVars* existing()
        {
            return ALCVars::instanceExists() ? ALCVars::getInstance() : nullptr;
        }
    }

    // 0 with no store, or none registered: nothing has changed.
    U32 generation(U32 index)
    {
        ALCVars* store = existing();
        return store && store->registered() && index < store->settingCount() ? store->generation(index) : 0;
    }

    U64 groupGeneration(U32 group)
    {
        ALCVars* store = existing();
        return store && store->registered() && group < store->groupCount() ? store->groupGeneration(group) : 0;
    }

    U64 tagGeneration(U32 tag)
    {
        ALCVars* store = existing();
        return store && store->registered() && tag < store->schema().tags.size() ? store->tagGeneration(tag) : 0;
    }

    boost::shared_ptr<const void> sharedDefault(const ALCVarDesc& desc)
    {
        // Kept for the process: a view of one may be held to the frame's end.
        static LLCoros::Mutex                                                              mutex;
        static boost::unordered_flat_map<const ALCVarDesc*, boost::shared_ptr<const void>> defaults;
        LLCoros::LockType lock(mutex);
        auto&             value = defaults[&desc];
        if (!value)
        {
            value = makeShared(ALCVars::defaultOf(desc));
        }
        return value;
    }
}
