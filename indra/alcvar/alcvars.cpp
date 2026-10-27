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

#include "lldate.h"
#include "llfile.h"
#include "threadpool.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_node_map.hpp>
#include <boost/smart_ptr/make_shared.hpp>

#include <fmt/format.h>

namespace
{
    // The calendar time in milliseconds: a change's `modified`, and the
    // session a history row names.
    S64 nowMs()
    {
        return S64(LLDate::now().secondsSinceEpoch() * 1000.0);
    }

    // A name of its own for each flusher a store starts: a pool's name is its
    // key, and its queue's, among the live ones, and a second is fatal.
    std::atomic<U32> sFlushers{ 0 };

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

// The flusher's thread: a pool of one, whose run() is the flusher's loop.
// The store closes it after its last flush, not the viewer's quitting, which
// would leave changes unwritten.
class ALCVars::Flusher final : public LL::ThreadPool
{
public:
    Flusher(const std::string& name, ALCVars& store)
    :   LL::ThreadPool(name, 1, 1, /*auto_shutdown*/ false, /*fixed_width*/ true),
        mStore(store)
    {
    }

    ~Flusher() override { close(); }

    void run() override { mStore.flusherLoop(); }

private:
    ALCVars& mStore;
};

namespace
{
    // Every store's own number: a batch kept past its store ends nothing on
    // the next one.
    std::atomic<U64> sStores{ 0 };
}

ALCVars::ALCVars()
:   mSerial(++sStores),
    mSession(nowMs())
{
}

ALCVars::~ALCVars()
{
    close();

    // The listeners go first, while the store is whole: one that owns
    // another's connection lets it go here, and its disconnect finds the
    // store tearing down rather than half destroyed.
    mTearingDown = true;
    {
        auto listeners      = std::move(mListeners);
        auto groupListeners = std::move(mGroupListeners);
    }

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
            setting.def = defaultOf(desc);
            mSettings.push_back(std::move(setting));
        }
        ALCVarAtomic::storeRelease(&group.state->registered, U32(1));
    }

    mGenerations.reset(new std::atomic<U32>[mSettings.size()]());
    mGroupGenerations.reset(new std::atomic<U64>[mGroups.size()]());
    mTagGenerations.reset(new std::atomic<U64>[std::max<size_t>(schema.tags.size(), 1)]());
    mFlags.reset(new std::atomic<U8>[mSettings.size()]());
    mWriteSeq.reset(new std::atomic<U32>[mSettings.size()]());
    mListened.reset(new std::atomic<U8>[mSettings.size()]());
    mListeners.resize(mSettings.size());
    mGroupListeners.resize(mGroups.size());

    U32 aliases = 0;
    for (U32 i = 0; i < U32(mSettings.size()); ++i)
    {
        const ALCVarDesc& desc = *mSettings[i].desc;
        const bool        added = mNames.emplace(desc.name, Named{ i, nullptr, 0 }).second;
        llassert_always_msg(added, "a setting's name is its alone");
        for (const ALCVarAlias& alias : desc.aliases)
        {
            const bool aliased = mNames.emplace(alias.name, Named{ i, &alias, aliases++ }).second;
            llassert_always_msg(aliased, "an old name is one setting's");
        }
    }
    mAliasSaid.reset(new std::atomic<U8>[std::max<U32>(aliases, 1)]());

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
            mBoxes.push_back(std::make_unique<LLSD>(setting.def.get<LLSD>()));
            static_cast<ALCVarBoxedSlot*>(setting.slot)->value = mBoxes.back().get();
            continue;
        }
        publish(i, setting.def);
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
            if (auto old = slot.value.exchange(std::move(shared)))
            {
                retire(std::move(old));
            }
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
    return mSettings[index].def;
}

bool ALCVars::inGroup(U32 index, U32 group) const
{
    for (U32 g = mSettings[index].group; g != NONE; g = mGroups[g].parent)
    {
        if (g == group)
        {
            return true;
        }
    }
    return false;
}

LLCoros::LockType ALCVars::lockWrites() const
{
    mLocks.fetch_add(1, std::memory_order_relaxed);
    return LLCoros::LockType(mWriteMutex);
}

bool ALCVars::readsAccount(U32 index) const
{
    const ALCVarDesc& desc = *mSettings[index].desc;
    return desc.scope == ALCVarScope::Account || desc.has(ALCVAR_ACCOUNT_OVERRIDE);
}

bool ALCVars::readsGlobal(U32 index) const
{
    return mSettings[index].desc->scope != ALCVarScope::Account;
}

ALCVarLayer ALCVars::userLayer(U32 index) const
{
    const ALCVarDesc& desc = *mSettings[index].desc;
    if (desc.scope == ALCVarScope::Account)
    {
        return ALCVarLayer::Account;
    }
    if (desc.has(ALCVAR_ACCOUNT_OVERRIDE) && layer(ALCVarLayer::Account).contains(index))
    {
        return ALCVarLayer::Account;
    }
    return ALCVarLayer::Global;
}

ALCVarValue ALCVars::effective(U32 index) const
{
    const ALCVarValue* top = nullptr;
    if (const auto held = mHolds.find(index); held != mHolds.end() && !held->second.empty())
    {
        top = &held->second.back().value;
    }
    else
    {
        for (const ALCVarLayer which :
             { ALCVarLayer::Session, ALCVarLayer::Account, ALCVarLayer::Global, ALCVarLayer::Quality })
        {
            if ((which == ALCVarLayer::Account && !readsAccount(index)) ||
                (which == ALCVarLayer::Global && !readsGlobal(index)))
            {
                continue;
            }
            const Layer& values = layer(which);
            if (const auto found = values.find(index); found != values.end())
            {
                top = &found->second;
                break;
            }
        }
    }
    ALCVarValue value = top ? *top : mSettings[index].def;

    // Every owner's bounds, over a hold as over the layers, and kept within
    // the declared range.
    const auto clamps = mClamps.find(index);
    if (clamps == mClamps.end() || clamps->second.empty())
    {
        return value;
    }
    F64 lowest  = -std::numeric_limits<F64>::max();
    F64 highest = std::numeric_limits<F64>::max();
    for (const Clamp& clamp : clamps->second)
    {
        lowest  = std::max(lowest, clamp.lowest);
        highest = std::min(highest, clamp.highest);
    }
    value = ALCVarValues::clampNumber(value, lowest, highest);
    const ALCVarDesc& d = *mSettings[index].desc;
    return d.ranged ? ALCVarValues::clampNumber(value, d.rangeMin, d.rangeMax) : value;
}

bool ALCVars::slotHolds(U32 index, const ALCVarValue& value) const
{
    const Setting& setting = mSettings[index];
    if (value.empty() || value.type() != setting.desc->type)
    {
        return false;
    }
    switch (setting.storage)
    {
        case ALCVarStorage::Byte:
            return ALCVarAtomic::loadRelaxed(static_cast<const U8*>(setting.slot)) == U8(encodeWords(value)[0]);
        case ALCVarStorage::Word:
            return ALCVarAtomic::loadRelaxed(static_cast<const U32*>(setting.slot)) == U32(encodeWords(value)[0]);
        case ALCVarStorage::DWord:
            return ALCVarAtomic::loadRelaxed(static_cast<const U64*>(setting.slot)) == encodeWords(value)[0];
        case ALCVarStorage::Pair:
        {
            U64 words[2];
            ALCVarDetail::readWords(*static_cast<const ALCVarSeqSlot<2>*>(setting.slot), words);
            const auto expected = encodeWords(value);
            return words[0] == expected[0] && words[1] == expected[1];
        }
        case ALCVarStorage::Triple:
        {
            U64 words[3];
            ALCVarDetail::readWords(*static_cast<const ALCVarSeqSlot<3>*>(setting.slot), words);
            const auto expected = encodeWords(value);
            return words[0] == expected[0] && words[1] == expected[1] && words[2] == expected[2];
        }
        case ALCVarStorage::Shared:
        {
            const auto current = static_cast<const ALCVarSharedSlot*>(setting.slot)->value.load();
            if (!current)
            {
                return false;
            }
            if (setting.desc->type == ALCVarType::String)
            {
                return *static_cast<const std::string*>(current.get()) == value.get<std::string>();
            }
            return fromShared(setting.desc->type, current.get()) == value;
        }
        case ALCVarStorage::Boxed:
            return readSlot(index) == value;
    }
    return false;
}

void ALCVars::beginSettingWrite(U32 index)
{
    const U32 sequence = mWriteSeq[index].load(std::memory_order_relaxed);
    mWriteSeq[index].store(sequence + 1, std::memory_order_relaxed);
    ALCVarAtomic::fenceRelease();
}

void ALCVars::endSettingWrite(U32 index)
{
    mWriteSeq[index].store(mWriteSeq[index].load(std::memory_order_relaxed) + 1, std::memory_order_release);
}

bool ALCVars::unchanged(U32 index, const ALCVarValue& value, U8 flags) const
{
    if (flags & (FLAG_SESSION | FLAG_OWNED))
    {
        return false;
    }
    // LLSD is the main thread's, and a write of it rare: always the long way.
    if (mSettings[index].storage == ALCVarStorage::Boxed || !slotHolds(index, value))
    {
        return false;
    }
    // The value the user layer holds, written again -- unless it is the
    // default, which a write would drop -- or the default with no layer
    // below the user's giving another, which a write would not keep.
    if (flags & FLAG_USER)
    {
        return !(value == mSettings[index].def);
    }
    return !(flags & FLAG_QUALITY);
}

bool ALCVars::validate(U32 index, ALCVarValue& value) const
{
    const ALCVarDesc& desc = *mSettings[index].desc;
    if (value.empty() || value.type() != desc.type || !ALCVarValues::finiteValue(value))
    {
        return false;
    }
    if (desc.ranged)
    {
        const ALCVarValue clamped = ALCVarValues::clampNumber(value, desc.rangeMin, desc.rangeMax);
        if (!(clamped == value))
        {
            LL_DEBUGS("ALCVars") << desc.name << ": " << value.describe() << " is outside its range; "
                                 << clamped.describe() << " kept" << LL_ENDL;
            value = clamped;
        }
    }
    if (!desc.choices.empty())
    {
        bool chosen = false;
        for (const ALCVarChoice& choice : desc.choices)
        {
            chosen = chosen || (desc.type == ALCVarType::String ? value.get<std::string>() == choice.text
                                                                 : ALCVarValues::asInteger(value) == choice.number);
        }
        if (!chosen)
        {
            return false;
        }
    }
    if (!desc.asset.empty())
    {
        // A file the setting names is a bare name in its kind's folder, and
        // one Windows opens as a file: not NUL or COM1, whatever follows them.
        const std::string& name = value.get<std::string>();
        if (name.find_first_of("/\\:") != std::string::npos || name.find("..") != std::string::npos ||
            (!name.empty() && (name[0] == '.' || !LLFile::isSafeFileName(name))))
        {
            LL_WARNS("ALCVars") << desc.name << ": refused \"" << name << "\", which is not a bare file name"
                                << LL_ENDL;
            return false;
        }
    }
    return true;
}

void ALCVars::updateFlags(U32 index)
{
    U8 flags = 0;
    if (layer(userLayer(index)).contains(index))
    {
        flags |= FLAG_USER;
    }
    if (layer(ALCVarLayer::Quality).contains(index))
    {
        flags |= FLAG_QUALITY;
    }
    if (layer(ALCVarLayer::Session).contains(index))
    {
        flags |= FLAG_SESSION;
    }
    const auto held    = mHolds.find(index);
    const auto clamped = mClamps.find(index);
    if ((held != mHolds.end() && !held->second.empty()) || (clamped != mClamps.end() && !clamped->second.empty()))
    {
        flags |= FLAG_OWNED;
    }
    if (restricted(mDenied, index))
    {
        flags |= FLAG_DENIED;
    }
    if (restricted(mHidden, index))
    {
        flags |= FLAG_HIDDEN;
    }
    mFlags[index].store(flags, std::memory_order_relaxed);
}

void ALCVars::updateAllFlags()
{
    for (U32 i = 0; i < U32(mSettings.size()); ++i)
    {
        beginSettingWrite(i);
        updateFlags(i);
        endSettingWrite(i);
    }
}

bool ALCVars::restricted(const boost::unordered_flat_map<U64, Owners>& restrictions, U32 index) const
{
    if (restrictions.empty())
    {
        return false;
    }
    if (restrictions.contains(keyOf(ALCVarTarget::setting(index))))
    {
        return true;
    }
    for (U32 g = mSettings[index].group; g != NONE; g = mGroups[g].parent)
    {
        if (restrictions.contains(keyOf(ALCVarTarget::group(g))))
        {
            return true;
        }
    }
    return false;
}

void ALCVars::apply(std::span<const U32> indices, std::vector<Change>* changes)
{
    // Each setting's sequence odd from its flags to its slot; the indices
    // are distinct.
    std::vector<std::pair<U32, ALCVarValue>> changed;
    for (const U32 index : indices)
    {
        beginSettingWrite(index);
        updateFlags(index);
        ALCVarValue now = effective(index);
        if (!slotHolds(index, now))
        {
            changed.emplace_back(index, std::move(now));
        }
    }
    if (changed.empty())
    {
        for (const U32 index : indices)
        {
            endSettingWrite(index);
        }
        return;
    }

    std::vector<U32> groups;
    for (const auto& [index, value] : changed)
    {
        const U32 group = mSettings[index].group;
        if (std::find(groups.begin(), groups.end(), group) == groups.end())
        {
            groups.push_back(group);
        }
    }
    for (const U32 group : groups)
    {
        beginGroupWrite(*mGroups[group].desc->state);
    }
    for (const auto& [index, value] : changed)
    {
        if (changes && listened(index))
        {
            changes->push_back({ index, readSlot(index) });
        }
        publish(index, value);
    }
    for (const U32 group : groups)
    {
        endGroupWrite(*mGroups[group].desc->state);
    }
    for (const U32 index : indices)
    {
        endSettingWrite(index);
    }
    for (const auto& [index, value] : changed)
    {
        bump(index);
    }
    mChanges.fetch_add(changed.size(), std::memory_order_relaxed);
    // Off the main thread, queued now, under the lock: in the order made.
    if (changes && !changes->empty() && !on_main_thread())
    {
        queueChanges(*changes);
    }
}

void ALCVars::bump(U32 index)
{
    mGenerations[index].fetch_add(1, std::memory_order_release);
    for (const U32 tag : mSettings[index].tags)
    {
        mTagGenerations[tag].fetch_add(1, std::memory_order_release);
    }
    for (U32 g = mSettings[index].group; g != NONE; g = mGroups[g].parent)
    {
        mGroupGenerations[g].fetch_add(1, std::memory_order_release);
    }
}

void ALCVars::retire(boost::shared_ptr<const void> value)
{
    LLCoros::LockType lock(mRetiredMutex);
    mRetired.push_back(std::move(value));
}

void ALCVars::endFrame()
{
    std::vector<boost::shared_ptr<const void>> retired;
    {
        LLCoros::LockType lock(mRetiredMutex);
        retired.swap(mRetired);
    }
}

size_t ALCVars::retiredCount() const
{
    LLCoros::LockType lock(mRetiredMutex);
    return mRetired.size();
}

ALCVarStats ALCVars::stats() const
{
    return { mLocks.load(std::memory_order_relaxed), mChanges.load(std::memory_order_relaxed) };
}

bool ALCVars::set(U32 index, const ALCVarValue& input)
{
    // What can be told before the lock: the flags and the slot, read between
    // two reads of the setting's write sequence, so that a write under the
    // lock meanwhile -- a hold, a Session value placed -- sends this one the
    // long way rather than letting a stale flag pass a fresh value. A write
    // posted to the main thread never takes this way: it would pass the
    // writes posted before it.
    const bool posts    = postsToMain(index);
    const U32  sequence = mWriteSeq[index].load(std::memory_order_acquire);
    const U8   flags    = mFlags[index].load(std::memory_order_relaxed);
    if (flags & FLAG_DENIED)
    {
        return false;
    }
    if (!posts && !(sequence & 1) && unchanged(index, input, flags))
    {
        ALCVarAtomic::fenceAcquire();
        if (mWriteSeq[index].load(std::memory_order_relaxed) == sequence)
        {
            return true;
        }
    }
    ALCVarValue value = input;
    if (!validate(index, value))
    {
        return false;
    }
    llassert(mSettings[index].storage != ALCVarStorage::Boxed || on_main_thread());
    if (posts)
    {
        post(index, PostedWrite::Kind::Set, std::move(value));
        return true;
    }
    if (!runValidator(index, value))
    {
        return false;
    }

    std::vector<Change> changes;
    {
        auto lock = lockWrites();
        if (restricted(mDenied, index))
        {
            return false;
        }
        const ALCVarLayer user = userLayer(index);
        layer(ALCVarLayer::Session).erase(index);
        // The bottom user layer keeps no value the compiled default already
        // gives; an account's override keeps any.
        const bool bottom = user == ALCVarLayer::Global || !mSettings[index].desc->has(ALCVAR_ACCOUNT_OVERRIDE);
        if (bottom && value == mSettings[index].def && !layer(ALCVarLayer::Quality).contains(index))
        {
            putUser(user, index, std::nullopt);
        }
        else
        {
            putUser(user, index, std::move(value));
        }
        const U32 one[] = { index };
        apply(one, &changes);
    }
    noteChanges(changes);
    return true;
}

void ALCVars::reset(U32 index)
{
    if (postsToMain(index))
    {
        post(index, PostedWrite::Kind::Reset);
        return;
    }
    std::vector<Change> changes;
    {
        auto lock = lockWrites();
        if (restricted(mDenied, index))
        {
            return;
        }
        putUser(userLayer(index), index, std::nullopt);
        const U32 one[] = { index };
        apply(one, &changes);
    }
    noteChanges(changes);
}

void ALCVars::resetGroup(U32 group)
{
    std::vector<Change> changes;
    {
        auto             lock = lockWrites();
        std::vector<U32> touched;
        for (U32 i = 0; i < U32(mSettings.size()); ++i)
        {
            if (!inGroup(i, group) || restricted(mDenied, i))
            {
                continue;
            }
            if (postsToMain(i))
            {
                post(i, PostedWrite::Kind::Reset);
                continue;
            }
            putUser(userLayer(i), i, std::nullopt);
            touched.push_back(i);
        }
        apply(touched, &changes);
    }
    noteChanges(changes);
}

bool ALCVars::isUserSet(U32 index) const
{
    return (mFlags[index].load(std::memory_order_relaxed) & FLAG_USER) != 0;
}

bool ALCVars::setSession(U32 index, const ALCVarValue& input)
{
    ALCVarValue value = input;
    if (!validate(index, value))
    {
        return false;
    }
    if (postsToMain(index))
    {
        post(index, PostedWrite::Kind::Session, std::move(value));
        return true;
    }
    if (!runValidator(index, value))
    {
        return false;
    }
    std::vector<Change> changes;
    {
        auto lock = lockWrites();
        layer(ALCVarLayer::Session).insert_or_assign(index, std::move(value));
        const U32 one[] = { index };
        apply(one, &changes);
    }
    noteChanges(changes);
    return true;
}

void ALCVars::clearSession(U32 index)
{
    if (postsToMain(index))
    {
        post(index, PostedWrite::Kind::ClearSession);
        return;
    }
    std::vector<Change> changes;
    {
        auto lock = lockWrites();
        layer(ALCVarLayer::Session).erase(index);
        const U32 one[] = { index };
        apply(one, &changes);
    }
    noteChanges(changes);
}

void ALCVars::setQuality(std::span<const std::pair<U32, ALCVarValue>> values, bool dropUser)
{
    std::vector<Change> changes;
    auto                lock = lockWrites();
    std::vector<U32> touched;
    for (const auto& [index, value] : layer(ALCVarLayer::Quality))
    {
        touched.push_back(index);
    }
    Layer next;
    for (const auto& [index, input] : values)
    {
        ALCVarValue value = input;
        if (index >= mSettings.size() || !validate(index, value))
        {
            LL_WARNS("ALCVars") << "a quality level's value is refused: "
                                << (index < mSettings.size() ? desc(index).name : std::string_view("?")) << " = "
                                << input.describe() << LL_ENDL;
            continue;
        }
        next.insert_or_assign(index, std::move(value));
        if (dropUser && !restricted(mDenied, index))
        {
            putUser(ALCVarLayer::Global, index, std::nullopt);
            putUser(ALCVarLayer::Account, index, std::nullopt);
        }
        if (std::find(touched.begin(), touched.end(), index) == touched.end())
        {
            touched.push_back(index);
        }
    }
    layer(ALCVarLayer::Quality) = std::move(next);
    apply(touched, &changes);
    lock.unlock();
    noteChanges(changes);
}

ALCVarLayer ALCVars::layerOf(U32 index) const
{
    auto lock = lockWrites();
    for (const ALCVarLayer which :
         { ALCVarLayer::Session, ALCVarLayer::Account, ALCVarLayer::Global, ALCVarLayer::Quality })
    {
        if ((which == ALCVarLayer::Account && !readsAccount(index)) ||
            (which == ALCVarLayer::Global && !readsGlobal(index)))
        {
            continue;
        }
        if (layer(which).contains(index))
        {
            return which;
        }
    }
    return ALCVarLayer::Default;
}

std::optional<ALCVarValue> ALCVars::layerValue(U32 index, ALCVarLayer which) const
{
    if (which == ALCVarLayer::Default)
    {
        return mSettings[index].def;
    }
    auto         lock   = lockWrites();
    const Layer& values = layer(which);
    if (const auto found = values.find(index); found != values.end())
    {
        return found->second;
    }
    return std::nullopt;
}

void ALCVars::hold(std::string_view owner, U32 index, const ALCVarValue& value)
{
    // Fitted as a write is: a reader indexing by choice never sees another.
    ALCVarValue held = value;
    if (!validate(index, held))
    {
        LL_WARNS("ALCVars") << owner << " cannot hold " << desc(index).name << " at " << value.describe() << LL_ENDL;
        return;
    }
    std::vector<Change> changes;
    {
        auto  lock  = lockWrites();
        auto& holds = mHolds[index];
        std::erase_if(holds, [owner](const Hold& hold) { return hold.owner == owner; });
        holds.push_back({ std::string(owner), std::move(held) });
        const U32 one[] = { index };
        apply(one, &changes);
    }
    noteChanges(changes);
}

void ALCVars::clamp(std::string_view owner, U32 index, F64 lowest, F64 highest)
{
    if (!ALCVarTypes::isNumber(desc(index).type))
    {
        LL_WARNS("ALCVars") << owner << " cannot clamp " << desc(index).name << ", which is not a number" << LL_ENDL;
        return;
    }
    if (!ALCVarValues::finite(lowest) || !ALCVarValues::finite(highest))
    {
        LL_WARNS("ALCVars") << owner << " cannot clamp " << desc(index).name << " to " << lowest << ".." << highest
                            << ": its bounds are finite" << LL_ENDL;
        return;
    }
    std::vector<Change> changes;
    {
        auto  lock   = lockWrites();
        auto& clamps = mClamps[index];
        std::erase_if(clamps, [owner](const Clamp& clamp) { return clamp.owner == owner; });
        clamps.push_back({ std::string(owner), lowest, highest });
        const U32 one[] = { index };
        apply(one, &changes);
    }
    noteChanges(changes);
}

void ALCVars::release(std::string_view owner, U32 index)
{
    std::vector<Change> changes;
    auto                lock = lockWrites();
    if (const auto holds = mHolds.find(index); holds != mHolds.end())
    {
        std::erase_if(holds->second, [owner](const Hold& hold) { return hold.owner == owner; });
    }
    if (const auto clamps = mClamps.find(index); clamps != mClamps.end())
    {
        std::erase_if(clamps->second, [owner](const Clamp& clamp) { return clamp.owner == owner; });
    }
    const U32 one[] = { index };
    apply(one, &changes);
    lock.unlock();
    noteChanges(changes);
}

void ALCVars::restrict(boost::unordered_flat_map<U64, Owners>& restrictions, std::string_view owner,
                       ALCVarTarget target, bool add)
{
    auto       lock   = lockWrites();
    const U64  key    = keyOf(target);
    Owners&    owners = restrictions[key];
    const auto found  = std::find(owners.begin(), owners.end(), owner);
    if (add && found == owners.end())
    {
        owners.emplace_back(owner);
    }
    else if (!add && found != owners.end())
    {
        owners.erase(found);
    }
    if (owners.empty())
    {
        restrictions.erase(key);
    }
    updateAllFlags();
}

void ALCVars::denyWrite(std::string_view owner, ALCVarTarget target)
{
    restrict(mDenied, owner, target, true);
}

void ALCVars::allowWrite(std::string_view owner, ALCVarTarget target)
{
    restrict(mDenied, owner, target, false);
}

void ALCVars::hide(std::string_view owner, ALCVarTarget target)
{
    restrict(mHidden, owner, target, true);
}

void ALCVars::unhide(std::string_view owner, ALCVarTarget target)
{
    restrict(mHidden, owner, target, false);
}

void ALCVars::releaseAll(std::string_view owner)
{
    std::vector<Change> changes;
    auto                lock = lockWrites();
    std::vector<U32> touched;
    for (auto& [index, holds] : mHolds)
    {
        if (std::erase_if(holds, [owner](const Hold& hold) { return hold.owner == owner; }))
        {
            touched.push_back(index);
        }
    }
    for (auto& [index, clamps] : mClamps)
    {
        if (std::erase_if(clamps, [owner](const Clamp& clamp) { return clamp.owner == owner; }) &&
            std::find(touched.begin(), touched.end(), index) == touched.end())
        {
            touched.push_back(index);
        }
    }
    for (auto* restrictions : { &mDenied, &mHidden })
    {
        for (auto& [key, owners] : *restrictions)
        {
            std::erase(owners, owner);
        }
        boost::unordered::erase_if(*restrictions, [](const auto& entry) { return entry.second.empty(); });
    }
    updateAllFlags();
    apply(touched, &changes);
    lock.unlock();
    noteChanges(changes);
}

bool ALCVars::isHeld(U32 index) const
{
    return (mFlags[index].load(std::memory_order_relaxed) & FLAG_OWNED) != 0;
}

bool ALCVars::isWriteDenied(U32 index) const
{
    return (mFlags[index].load(std::memory_order_relaxed) & FLAG_DENIED) != 0;
}

bool ALCVars::isHidden(U32 index) const
{
    return desc(index).has(ALCVAR_HIDDEN) || (mFlags[index].load(std::memory_order_relaxed) & FLAG_HIDDEN) != 0;
}

std::vector<std::string> ALCVars::ownersOf(U32 index) const
{
    auto                     lock = lockWrites();
    std::vector<std::string> owners;
    auto                     add = [&owners](const std::string& owner) {
        if (std::find(owners.begin(), owners.end(), owner) == owners.end())
        {
            owners.push_back(owner);
        }
    };
    if (const auto holds = mHolds.find(index); holds != mHolds.end())
    {
        for (const Hold& hold : holds->second)
        {
            add(hold.owner);
        }
    }
    if (const auto clamps = mClamps.find(index); clamps != mClamps.end())
    {
        for (const Clamp& clamp : clamps->second)
        {
            add(clamp.owner);
        }
    }
    for (const auto* restrictions : { &mDenied, &mHidden })
    {
        for (const auto& [key, keyOwners] : *restrictions)
        {
            const bool group = (key >> 32) != 0;
            const U32  which = U32(key);
            if ((!group && which == index) || (group && inGroup(index, which)))
            {
                for (const auto& owner : keyOwners)
                {
                    add(owner);
                }
            }
        }
    }
    return owners;
}

// ---- files ----------------------------------------------------------------

namespace
{
    // A scope move applied to an account, as its meta records it.
    std::string movedKey(std::string_view name)
    {
        return "moved:" + std::string(name);
    }
}

void ALCVars::putUser(ALCVarLayer which, U32 index, std::optional<ALCVarValue> value)
{
    Layer&                     values = layer(which);
    std::optional<ALCVarValue> before;
    if (const auto found = values.find(index); found != values.end())
    {
        before = found->second;
    }
    if (value)
    {
        values.insert_or_assign(index, *value);
    }
    else
    {
        values.erase(index);
    }
    if (before.has_value() != value.has_value() || (before && !(*before == *value)))
    {
        markDirty(which, index, std::move(before), std::move(value));
    }
}

void ALCVars::markDirty(ALCVarLayer which, U32 index, std::optional<ALCVarValue> before,
                        std::optional<ALCVarValue> after)
{
    if (desc(index).scope == ALCVarScope::Runtime)
    {
        return;
    }
    // An account's value before its login succeeds is kept for that login.
    if (which == ALCVarLayer::Account && !mAccountConfirmed)
    {
        mAccountPending.insert(index);
        return;
    }
    if (before)
    {
        before = ALCVarFile::persisted(*before);
    }
    if (after)
    {
        after = ALCVarFile::persisted(*after);
    }
    LLCoros::LockType lock(mPersistMutex);
    Scope&            scope = which == ALCVarLayer::Account ? mAccount : mGlobal;
    const auto        now   = std::chrono::steady_clock::now();
    if (!mUnflushed)
    {
        mFirstDirty = now;
    }
    mUnflushed                = true;
    mLastDirty                = now;
    const auto [entry, added] = scope.dirty.try_emplace(index);
    if (added)
    {
        entry->second.before = std::move(before);
    }
    entry->second.after    = std::move(after);
    entry->second.modified = nowMs();
    mFlushWake.notify_one();
}

ALCVarRow ALCVars::rowOf(U32 index, const std::optional<ALCVarValue>& value, S64 modified) const
{
    const ALCVarDesc& d = desc(index);
    ALCVarRow         row;
    row.name      = std::string(d.name);
    row.type      = S32(d.type);
    row.unit      = std::string(ALCVarUnits::nameOf(d.unit));
    row.decl      = d.decl();
    row.tombstone = !value;
    row.value     = value ? ALCVarFile::persisted(*value) : ALCVarValue();
    row.modified  = modified;
    return row;
}

ALCVars::Loaded ALCVars::load(const std::vector<ALCVarRow>& rows, const std::vector<ALCVarRow>& prior,
                              ALCVarLayer which) const
{
    Loaded                                                  loaded;
    boost::unordered_flat_map<std::string_view, const ALCVarRow*> byName;
    for (const ALCVarRow& row : rows)
    {
        byName.emplace(row.name, &row);
        loaded.rows.insert(row.name);
    }
    // What a hook reads, by any name -- a setting's sources among them: each
    // kept where it is for the whole load, under a name of its own, so that
    // what a hook was given stays valid through its later lookups.
    boost::unordered_node_map<std::string, ALCVarStored> stored;
    const ALCVarStoredLookup lookup = [&](std::string_view name) -> const ALCVarStored* {
        const auto row = byName.find(name);
        if (row == byName.end())
        {
            return nullptr;
        }
        if (const auto cached = stored.find(row->second->name); cached != stored.end())
        {
            return &cached->second;
        }
        const auto value = row->second->stored();
        return value ? &stored.emplace(row->second->name, *value).first->second : nullptr;
    };
    const ALCVarMigrationLookup migrations = [this](std::string_view name) { return migration(name); };

    for (U32 i = 0; i < U32(mSettings.size()); ++i)
    {
        const ALCVarDesc& d = desc(i);
        if (d.scope == ALCVarScope::Runtime)
        {
            continue;
        }
        const bool mine = which == ALCVarLayer::Global ? d.scope == ALCVarScope::Global
                                                       : d.scope == ALCVarScope::Account || d.has(ALCVAR_ACCOUNT_OVERRIDE);

        // The newest of its own row and its old names': a value the user
        // reset does not come back through an old name, and a change an
        // older viewer made since does.
        const ALCVarRow*   winner = nullptr;
        const ALCVarAlias* via    = nullptr;
        if (const auto own = byName.find(d.name); own != byName.end())
        {
            winner = own->second;
        }
        for (const ALCVarAlias& alias : d.aliases)
        {
            const auto old = byName.find(alias.name);
            if (old != byName.end() && (!winner || old->second->modified > winner->modified))
            {
                winner = old->second;
                via    = &alias;
            }
        }

        std::optional<ALCVarValue> value;
        S64                        modified = 0;
        bool                       rewrite  = false;
        ALCVarConvert::Path        path;
        if (!winner)
        {
            // Nothing of its own: only its sources give it a value.
            if (d.from.empty() || !mine)
            {
                continue;
            }
            value    = ALCVarConvert::fromStored(nullptr, d, nullptr, migrations, lookup, &path);
            modified = nowMs();
            rewrite  = path.known;
        }
        else if (winner->tombstone)
        {
            continue;
        }
        else
        {
            const auto row = winner->stored();
            if (!row)
            {
                continue;
            }
            // A current row is fitted as it is read and never written back:
            // a newer viewer's wider range or added choice is not a step,
            // and writing the fitted value would clamp its value for good.
            const bool current = !via && winner->type == S32(d.type) && winner->decl == d.decl() &&
                                 winner->unit == ALCVarUnits::nameOf(d.unit);
            value    = current ? ALCVarConvert::fit(row->value, d)
                               : ALCVarConvert::fromStored(&*row, d, via, migrations, lookup, &path);
            modified = winner->modified;
            // A row of a step this build does not know -- a newer viewer's,
            // another branch's -- is left as it was written: nothing
            // converts backwards, and the viewer that wrote it would bring a
            // rewrite forward a second time. Where that viewer ran a migrate
            // it kept the row from before it, which this build reads if it
            // knows that row's step.
            if (!current && !via && !path.known)
            {
                const ALCVarRow* known = nullptr;
                for (const ALCVarRow& kept : prior)
                {
                    if (kept.name == d.name && ALCVarConvert::stepOf(d, kept.decl) &&
                        (!known || kept.modified > known->modified))
                    {
                        known = &kept;
                    }
                }
                const auto earlier = known ? known->stored() : std::nullopt;
                if (earlier)
                {
                    ALCVarConvert::Path unused;
                    value = ALCVarConvert::fromStored(&*earlier, d, nullptr, migrations, lookup, &unused);
                }
            }
            // Written forward only where its step moved: an old name's row
            // under the new name, the old row kept for older viewers.
            rewrite = path.known && path.moved;
            if (!value)
            {
                LL_INFOS("ALCVars") << winner->name << " does not convert into " << d.name
                                    << ", which takes its default; the row is left as it was" << LL_ENDL;
            }
        }
        if (!value)
        {
            continue;
        }
        if (mine)
        {
            if (rewrite)
            {
                loaded.rewrites.push_back(rowOf(i, *value, modified));
                ALCVarHistory entry;
                entry.at      = nowMs();
                entry.session = mSession;
                entry.name    = std::string(d.name);
                entry.kind    = ALCVarHistory::MIGRATED;
                if (!d.has(ALCVAR_SENSITIVE))
                {
                    entry.before = winner ? std::optional<ALCVarValue>(winner->value) : std::nullopt;
                    entry.after  = ALCVarFile::persisted(*value);
                }
                loaded.history.push_back(std::move(entry));
                // A migrate's rewrite keeps the row it replaces, for a
                // viewer that knows only that row's step.
                if (path.migrated && winner && !via)
                {
                    loaded.prior.push_back(*winner);
                }
            }
            loaded.values.insert_or_assign(i, std::move(*value));
        }
        else
        {
            loaded.others.emplace_back(i, std::make_pair(std::move(*value), modified));
        }
    }
    return loaded;
}

namespace
{
    // A flush the writer is done with: what it dropped, of the settings no
    // later flush carries, is kept for the next.
    template <typename Posted, typename Entries>
    void settle(Posted& posted, U64 flush, Entries& entries, bool committed)
    {
        LLCoros::LockType lock(posted.mutex);
        for (auto& [index, dirty] : entries)
        {
            const auto last = posted.last.find(index);
            if (last == posted.last.end() || last->second != flush)
            {
                continue;
            }
            posted.last.erase(last);
            if (!committed)
            {
                posted.dropped.insert_or_assign(index, std::move(dirty));
            }
        }
    }
}

void ALCVars::attach(Scope& scope, std::shared_ptr<ALCVarFile> file)
{
    scope.file     = std::move(file);
    scope.readOnly = scope.file->readOnly();
    scope.posted   = std::make_shared<Posted>();
}

bool ALCVars::pendingIn(const Scope& scope, U32 index) const
{
    if (scope.dirty.contains(index))
    {
        return true;
    }
    if (!scope.posted)
    {
        return false;
    }
    LLCoros::LockType lock(scope.posted->mutex);
    return scope.posted->dropped.contains(index);
}

void ALCVars::writeDirty(Scope& scope)
{
    // With no file, or one that may only be read, it waits in memory.
    if (!scope.file || scope.readOnly)
    {
        return;
    }
    Posted&           posted = *scope.posted;
    LLCoros::LockType lock(posted.mutex);
    // What a dropped flush carried goes again, under anything newer.
    for (auto& [index, lost] : posted.dropped)
    {
        const auto [entry, added] = scope.dirty.try_emplace(index, std::move(lost));
        if (!added)
        {
            entry->second.before = std::move(lost.before);
        }
    }
    posted.dropped.clear();
    if (scope.dirty.empty())
    {
        return;
    }

    const U64                  flush = ++posted.flushes;
    std::vector<ALCVarRow>     rows;
    std::vector<ALCVarHistory> history;
    for (const auto& [index, dirty] : scope.dirty)
    {
        posted.last.insert_or_assign(index, flush);
        rows.push_back(rowOf(index, dirty.after, dirty.modified));
        scope.rows.insert(rows.back().name);
        if (dirty.before.has_value() == dirty.after.has_value() && (!dirty.before || *dirty.before == *dirty.after))
        {
            continue;
        }
        // Of a sensitive setting, only that it changed.
        const bool sensitive = desc(index).has(ALCVAR_SENSITIVE);
        history.push_back({ dirty.modified, mSession, rows.back().name, sensitive ? std::nullopt : dirty.before,
                            sensitive ? std::nullopt : dirty.after });
    }
    lock.unlock();

    // The changes are kept until the writer says whether they committed.
    auto entries = std::make_shared<boost::unordered_flat_map<U32, Dirty>>(std::move(scope.dirty));
    scope.dirty.clear();
    if (!scope.file->write(std::move(rows), std::move(history), {},
                           [posted = scope.posted, flush, entries](bool committed) {
                               settle(*posted, flush, *entries, committed);
                           }))
    {
        settle(posted, flush, *entries, false);
    }
}

void ALCVars::startFlusher()
{
    if (!mFlusher)
    {
        mFlusher = std::make_unique<Flusher>(fmt::format("ALCVars:flush#{}", ++sFlushers), *this);
        mFlusher->start();
    }
}

void ALCVars::flusherLoop()
{
    LLCoros::LockType lock(mPersistMutex);
    while (!mStopping)
    {
        if (!mUnflushed)
        {
            mFlushWake.wait(lock);
            continue;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto due = std::min(mLastDirty + mQuiet, mFirstDirty + mLongest);
        if (now < due)
        {
            mFlushWake.wait_until(lock, due);
            continue;
        }
        if (mBatchesOpen.load(std::memory_order_relaxed) > 0)
        {
            mFlushWake.wait_for(lock, std::chrono::milliseconds(10));
            continue;
        }
        mUnflushed = false;
        writeDirty(mGlobal);
        writeDirty(mAccount);
    }
}

void ALCVars::stopFlusher()
{
    {
        LLCoros::LockType lock(mPersistMutex);
        mStopping = true;
    }
    mFlushWake.notify_all();
    // Its loop ends at the stop: closing the pool joins the thread, which
    // only this thread, not the pool's own, may do.
    mFlusher.reset();
    mStopping = false;
}

void ALCVars::setFlushDelays(std::chrono::milliseconds quiet, std::chrono::milliseconds longest)
{
    LLCoros::LockType lock(mPersistMutex);
    mQuiet   = quiet;
    mLongest = longest;
    mFlushWake.notify_one();
}

namespace
{
    // Whether a file just opened is one the store can keep: on disk, and
    // opened.
    bool keepable(ALCVarFile& file)
    {
        const ALDbOpenResult& result = file.open();
        if (result.usable() && !file.database().transient())
        {
            return true;
        }
        LL_WARNS("ALCVars") << file.path() << " cannot be used: " << result.message << LL_ENDL;
        return false;
    }

    void unreadable(ALCVarFile& file)
    {
        LL_WARNS("ALCVars") << file.path() << " could not be read whole; it is left as it is, and nothing is written to it"
                            << LL_ENDL;
    }
}

bool ALCVars::openGlobal(const std::string& path, const ALCVarFileOptions& options)
{
    llassert(on_main_thread() && registered());
    auto file = std::make_shared<ALCVarFile>(path, options);
    if (!keepable(*file))
    {
        file->close();
        return false;
    }
    const auto rows  = file->readRows();
    const auto prior = rows ? file->readPrior() : std::nullopt;
    if (!prior)
    {
        unreadable(*file);
        file->close();
        return false;
    }
    Loaded loaded = load(*rows, *prior, ALCVarLayer::Global);

    std::vector<Change> changes;
    {
        auto             lock = lockWrites();
        std::vector<U32> touched;
        // A value written before the file opened is newer than the file's.
        for (auto& [index, value] : loaded.values)
        {
            if (!layer(ALCVarLayer::Global).contains(index))
            {
                layer(ALCVarLayer::Global).insert_or_assign(index, std::move(value));
                touched.push_back(index);
            }
        }
        mGlobalForAccounts.clear();
        for (auto& [index, value] : loaded.others)
        {
            mGlobalForAccounts.insert_or_assign(index, std::move(value));
        }
        apply(touched, &changes);
    }
    {
        LLCoros::LockType lock(mPersistMutex);
        attach(mGlobal, std::move(file));
        mGlobal.rows = std::move(loaded.rows);
        if (!mGlobal.readOnly)
        {
            mGlobal.file->bringForward(std::move(loaded.rewrites), std::move(loaded.prior),
                                       std::move(loaded.history), {});
        }
        startFlusher();
    }
    noteChanges(changes);
    return true;
}

bool ALCVars::openAccount(const std::string& path, const ALCVarFileOptions& options)
{
    llassert(on_main_thread() && registered());
    auto file = std::make_shared<ALCVarFile>(path, options);
    std::optional<boost::unordered_flat_map<std::string, std::string>> meta;
    std::optional<std::vector<ALCVarRow>>                              rows;
    std::optional<std::vector<ALCVarRow>>                              prior;
    const bool                                                         opened = keepable(*file);
    if (opened)
    {
        meta = file->readMeta();
        if (meta)
        {
            rows = file->readRows();
        }
        if (rows)
        {
            prior = file->readPrior();
        }
    }
    if (!prior)
    {
        if (opened)
        {
            unreadable(*file);
        }
        file->close();
        // The account open before this one is not the one logging in.
        if (accountOpen())
        {
            closeAccount();
        }
        return false;
    }
    Loaded loaded = load(*rows, *prior, ALCVarLayer::Account);

    std::shared_ptr<ALCVarFile>                      previous;
    std::vector<std::pair<std::string, std::string>> moved;
    std::vector<Change>                              changes;
    {
        auto lock = lockWrites();

        // Global to account: each account takes the global value of a
        // setting now an account's, once, where it has no row of its own.
        for (const auto& [index, value] : mGlobalForAccounts)
        {
            const ALCVarDesc& d   = desc(index);
            bool              has = loaded.rows.contains(std::string(d.name));
            for (const ALCVarAlias& alias : d.aliases)
            {
                has = has || loaded.rows.contains(std::string(alias.name));
            }
            if (has || meta->contains(movedKey(d.name)))
            {
                continue;
            }
            loaded.values.insert_or_assign(index, value.first);
            loaded.rewrites.push_back(rowOf(index, value.first, value.second));
            moved.emplace_back(movedKey(d.name), "1");
        }

        // What was written to account settings since the last login attempt
        // is laid over the account's own.
        Layer next = std::move(loaded.values);
        for (const U32 index : mAccountPending)
        {
            const auto written = layer(ALCVarLayer::Account).find(index);
            if (written != layer(ALCVarLayer::Account).end())
            {
                next.insert_or_assign(index, written->second);
            }
            else
            {
                next.erase(index);
            }
        }
        std::vector<U32> touched;
        for (const auto& [index, value] : layer(ALCVarLayer::Account))
        {
            touched.push_back(index);
        }
        for (const auto& [index, value] : next)
        {
            if (!layer(ALCVarLayer::Account).contains(index))
            {
                touched.push_back(index);
            }
        }
        layer(ALCVarLayer::Account) = std::move(next);
        mAccountOpen                = true;
        mAccountConfirmed           = false;
        updateAllFlags();
        apply(touched, &changes);

        // Account to global: the first account opened with a value of a
        // setting now global gives it to the global layer, if that has no
        // row.
        LLCoros::LockType persist(mPersistMutex);
        for (auto& [index, value] : loaded.others)
        {
            const ALCVarDesc& d   = desc(index);
            bool              has = layer(ALCVarLayer::Global).contains(index) ||
                       mGlobal.rows.contains(std::string(d.name)) || pendingIn(mGlobal, index);
            for (const ALCVarAlias& alias : d.aliases)
            {
                has = has || mGlobal.rows.contains(std::string(alias.name));
            }
            if (!has)
            {
                layer(ALCVarLayer::Global).insert_or_assign(index, value.first);
                const auto [entry, added] = mGlobal.dirty.try_emplace(index);
                entry->second.after       = ALCVarFile::persisted(value.first);
                entry->second.modified    = value.second;
                mLastDirty = mFirstDirty = std::chrono::steady_clock::now();
                mUnflushed                = true;
                const U32 one[]           = { index };
                apply(one, &changes);
            }
        }

        // The account before this one is written and closed.
        writeDirty(mAccount);
        previous = std::move(mAccount.file);
        attach(mAccount, std::move(file));
        mAccount.rows = std::move(loaded.rows);
        mAccount.dirty.clear();
        if (!mAccount.readOnly)
        {
            mAccount.file->bringForward(std::move(loaded.rewrites), std::move(loaded.prior), std::move(loaded.history),
                                        std::move(moved));
        }
        startFlusher();
        mFlushWake.notify_one();
    }
    if (previous)
    {
        previous->close();
    }
    noteChanges(changes);
    return true;
}

void ALCVars::confirmAccount()
{
    llassert(on_main_thread());
    auto lock = lockWrites();
    if (!mAccountOpen)
    {
        return;
    }
    mAccountConfirmed = true;
    for (const U32 index : mAccountPending)
    {
        const auto written = layer(ALCVarLayer::Account).find(index);
        markDirty(ALCVarLayer::Account, index, std::nullopt,
                  written != layer(ALCVarLayer::Account).end() ? std::optional<ALCVarValue>(written->second)
                                                               : std::nullopt);
    }
    mAccountPending.clear();
}

void ALCVars::closeAccount()
{
    llassert(on_main_thread());
    std::shared_ptr<ALCVarFile> file;
    std::vector<Change>         changes;
    {
        auto             lock = lockWrites();
        std::vector<U32> touched;
        for (const auto& [index, value] : layer(ALCVarLayer::Account))
        {
            touched.push_back(index);
        }
        layer(ALCVarLayer::Account).clear();
        mAccountPending.clear();
        mAccountOpen      = false;
        mAccountConfirmed = false;
        updateAllFlags();
        apply(touched, &changes);

        LLCoros::LockType persist(mPersistMutex);
        writeDirty(mAccount);
        file = std::move(mAccount.file);
        mAccount.dirty.clear();
        mAccount.posted.reset();
        mAccount.readOnly = false;
        mAccount.rows.clear();
    }
    // Its close writes what was posted, within the file's bounded wait.
    if (file)
    {
        file->close();
    }
    noteChanges(changes);
}

bool ALCVars::accountOpen() const
{
    auto lock = lockWrites();
    return mAccountOpen;
}

void ALCVars::moveToAccount(U32 index)
{
    llassert(desc(index).has(ALCVAR_ACCOUNT_OVERRIDE));
    std::vector<Change> changes;
    {
        auto lock = lockWrites();
        if (restricted(mDenied, index))
        {
            return;
        }
        const auto global = layer(ALCVarLayer::Global).find(index);
        putUser(ALCVarLayer::Account, index,
                global != layer(ALCVarLayer::Global).end() ? global->second : mSettings[index].def);
        const U32 one[] = { index };
        apply(one, &changes);
    }
    noteChanges(changes);
}

void ALCVars::moveToGlobal(U32 index)
{
    std::vector<Change> changes;
    {
        auto lock = lockWrites();
        if (restricted(mDenied, index))
        {
            return;
        }
        putUser(ALCVarLayer::Account, index, std::nullopt);
        const U32 one[] = { index };
        apply(one, &changes);
    }
    noteChanges(changes);
}

void ALCVars::flushNow()
{
    // Waited for by what the writers tell, not by the files, which a close
    // while this waits may let go.
    std::vector<LLCoros::Future<bool>> written;
    {
        LLCoros::LockType lock(mPersistMutex);
        mUnflushed = false;
        for (Scope* scope : { &mGlobal, &mAccount })
        {
            writeDirty(*scope);
            if (scope->file && !scope->readOnly)
            {
                written.push_back(scope->file->flushed());
            }
        }
    }
    // Both files within one wait: a writer that cannot write -- a disk that
    // hangs, a file another process holds locked -- goes on trying alone.
    const auto deadline = std::chrono::steady_clock::now() + ALCVarFile::CLOSE_WAIT;
    for (auto& result : written)
    {
        if (result.wait_until(deadline) != boost::fibers::future_status::ready)
        {
            LL_WARNS("ALCVars") << "settings not written within " << ALCVarFile::CLOSE_WAIT.count()
                                << " ms; the writer goes on trying" << LL_ENDL;
            return;
        }
    }
}

void ALCVars::close()
{
    stopFlusher();
    std::shared_ptr<ALCVarFile> global;
    std::shared_ptr<ALCVarFile> account;
    {
        LLCoros::LockType lock(mPersistMutex);
        writeDirty(mGlobal);
        writeDirty(mAccount);
        global  = std::move(mGlobal.file);
        account = std::move(mAccount.file);
        for (Scope* scope : { &mGlobal, &mAccount })
        {
            scope->posted.reset();
            scope->readOnly = false;
        }
    }
    for (auto* file : { account.get(), global.get() })
    {
        if (file)
        {
            file->close();
        }
    }
}

// ---- names ----------------------------------------------------------------

ALCVarRef ALCVars::find(std::string_view name) const
{
    const auto found = mNames.find(name);
    if (found == mNames.end())
    {
        return {};
    }
    const Named& named = found->second;
#if LL_DEBUG
    if (named.alias && !mAliasSaid[named.ordinal].exchange(1, std::memory_order_relaxed))
    {
        LL_DEBUGS("ALCVars") << named.alias->name << " is an old name of " << desc(named.index).name
                             << ", still used" << LL_ENDL;
    }
#endif
    return ALCVarRef(named.index, named.alias);
}

// ---- listeners ------------------------------------------------------------

namespace
{
    // Ids of every store's listeners: a connection kept past its store names
    // nothing the next store has.
    std::atomic<U64> sNextListener{ 1 };
}

ALCVarConnection ALCVars::listen(U32 index, ALCVarListener listener)
{
    llassert(on_main_thread());
    auto& list = mListeners[index];
    if (!list)
    {
        list = std::make_unique<Listeners<ALCVarListener>>();
    }
    const U64 id = sNextListener.fetch_add(1, std::memory_order_relaxed);
    list->entries.push_back(std::make_unique<Listeners<ALCVarListener>::Entry>(id, std::move(listener)));
    mListened[index].store(1, std::memory_order_relaxed);
    return ALCVarConnection(ALCVarConnection::Kind::Setting, index, id);
}

ALCVarConnection ALCVars::listenGroup(U32 group, ALCVarGroupListener listener)
{
    llassert(on_main_thread());
    auto& list = mGroupListeners[group];
    if (!list)
    {
        list = std::make_unique<Listeners<ALCVarGroupListener>>();
    }
    const U64 id = sNextListener.fetch_add(1, std::memory_order_relaxed);
    list->entries.push_back(std::make_unique<Listeners<ALCVarGroupListener>::Entry>(id, std::move(listener)));
    for (U32 i = 0; i < U32(mSettings.size()); ++i)
    {
        if (inGroup(i, group))
        {
            mListened[i].store(1, std::memory_order_relaxed);
        }
    }
    return ALCVarConnection(ALCVarConnection::Kind::Group, group, id);
}

void ALCVars::disconnect(ALCVarConnection::Kind kind, U32 index, U64 id)
{
    llassert(on_main_thread());
    if (mTearingDown)
    {
        return;
    }
    auto drop = [this, id](auto& list) {
        if (!list)
        {
            return;
        }
        for (auto& entry : list->entries)
        {
            if (entry->id == id)
            {
                entry->alive = false;
                mSwept       = false;
            }
        }
    };
    if (kind == ALCVarConnection::Kind::Setting && index < mListeners.size())
    {
        drop(mListeners[index]);
    }
    else if (kind == ALCVarConnection::Kind::Group && index < mGroupListeners.size())
    {
        drop(mGroupListeners[index]);
    }
    if (!mInPass)
    {
        sweep();
    }
}

bool ALCVars::connected(ALCVarConnection::Kind kind, U32 index, U64 id) const
{
    if (mTearingDown)
    {
        return false;
    }
    auto has = [id](const auto& list) {
        if (!list)
        {
            return false;
        }
        for (const auto& entry : list->entries)
        {
            if (entry->id == id && entry->alive)
            {
                return true;
            }
        }
        return false;
    };
    if (kind == ALCVarConnection::Kind::Setting && index < mListeners.size())
    {
        return has(mListeners[index]);
    }
    if (kind == ALCVarConnection::Kind::Group && index < mGroupListeners.size())
    {
        return has(mGroupListeners[index]);
    }
    return false;
}

void ALCVars::sweep()
{
    if (mSwept || mSweeping)
    {
        return;
    }
    // The dead are taken out of the lists first and let go after: a listener
    // that owns another's connection disconnects it as it goes, and that
    // disconnect's sweep is this one's next round, not one inside it.
    mSweeping = true;
    std::vector<std::unique_ptr<Listeners<ALCVarListener>::Entry>>      dead;
    std::vector<std::unique_ptr<Listeners<ALCVarGroupListener>::Entry>> deadGroup;
    auto prune = [](auto& list, auto& out) {
        if (!list)
        {
            return;
        }
        auto& entries = list->entries;
        for (auto& entry : entries)
        {
            if (!entry->alive)
            {
                out.push_back(std::move(entry));
            }
        }
        std::erase_if(entries, [](const auto& entry) { return !entry; });
        if (entries.empty())
        {
            list.reset();
        }
    };
    while (!mSwept)
    {
        mSwept = true;
        for (auto& list : mListeners)
        {
            prune(list, dead);
        }
        for (auto& list : mGroupListeners)
        {
            prune(list, deadGroup);
        }
        updateListened();
        dead.clear();
        deadGroup.clear();
    }
    mSweeping = false;
}

void ALCVars::updateListened()
{
    for (U32 i = 0; i < U32(mSettings.size()); ++i)
    {
        bool listened = mListeners[i] != nullptr;
        for (U32 g = mSettings[i].group; g != NONE && !listened; g = mGroups[g].parent)
        {
            listened = mGroupListeners[g] != nullptr;
        }
        mListened[i].store(listened ? 1 : 0, std::memory_order_relaxed);
        if (!listened)
        {
            mTold.erase(i);
        }
    }
}

void ALCVars::addPending(Change&& change)
{
    // Coalesced: a setting changed twice before listeners hear of it is told
    // once, from the value they were last given.
    if (mPendingAt.contains(change.index))
    {
        return;
    }
    mPendingAt.emplace(change.index, mPending.size());
    mPending.push_back(std::move(change));
}

void ALCVars::drainQueued()
{
    std::vector<Change> queued;
    {
        LLCoros::LockType lock(mQueueMutex);
        queued.swap(mQueued);
        mQueuedAt.clear();
    }
    for (Change& change : queued)
    {
        addPending(std::move(change));
    }
}

void ALCVars::queueChanges(std::vector<Change>& changes)
{
    LLCoros::LockType lock(mQueueMutex);
    for (Change& change : changes)
    {
        if (!mQueuedAt.contains(change.index))
        {
            mQueuedAt.emplace(change.index, mQueued.size());
            mQueued.push_back(std::move(change));
        }
    }
    changes.clear();
}

void ALCVars::noteChanges(std::vector<Change>& changes)
{
    if (changes.empty())
    {
        return;
    }
    llassert(on_main_thread());
    // What changed off the main thread first: its old values are older.
    drainQueued();
    for (Change& change : changes)
    {
        addPending(std::move(change));
    }
    if (mBatchDepth == 0 && !mInPass)
    {
        deliver();
    }
}

void ALCVars::deliver()
{
    struct Pass
    {
        explicit Pass(ALCVars& store) : mStore(store) { mStore.mInPass = true; }
        ~Pass()
        {
            mStore.mInPass = false;
            mStore.sweep();
        }
        ALCVars& mStore;
    } pass(*this);

    for (U32 round = 0; !mPending.empty(); ++round)
    {
        if (round >= MAX_PASS_ROUNDS)
        {
            std::string names;
            for (size_t i = 0; i < mPending.size() && i < 8; ++i)
            {
                names += (i ? ", " : "") + std::string(desc(mPending[i].index).name);
            }
            mPending.clear();
            mPendingAt.clear();
            LL_ERRS("ALCVars") << "listeners kept changing settings for " << MAX_PASS_ROUNDS
                               << " rounds of one pass: " << names << LL_ENDL;
            return;
        }

        std::vector<Change> changes;
        changes.swap(mPending);
        mPendingAt.clear();
        for (const Change& change : changes)
        {
            // From the value listeners were last given, where they have been
            // given one: a change queued before a value this pass read is not
            // news, nor is a write in this round of a setting told in it.
            const ALCVarValue now  = get(change.index);
            const auto        told = mTold.try_emplace(change.index, change.old).first;
            if (now == told->second)
            {
                continue;
            }
            const ALCVarValue old = std::exchange(told->second, now);
            // By index: a listener connected meanwhile may grow the list.
            if (const auto& list = mListeners[change.index])
            {
                for (size_t i = 0, count = list->entries.size(); i < count; ++i)
                {
                    auto* entry = list->entries[i].get();
                    if (entry->alive)
                    {
                        entry->fn(now, old);
                    }
                }
            }
            const ALCVarDesc& changed = desc(change.index);
            for (U32 g = mSettings[change.index].group; g != NONE; g = mGroups[g].parent)
            {
                if (const auto& list = mGroupListeners[g])
                {
                    for (size_t i = 0, count = list->entries.size(); i < count; ++i)
                    {
                        auto* entry = list->entries[i].get();
                        if (entry->alive)
                        {
                            entry->fn(changed);
                        }
                    }
                }
            }
        }
    }
}

void ALCVars::beginBatch()
{
    llassert(on_main_thread());
    ++mBatchDepth;
    mBatchesOpen.fetch_add(1, std::memory_order_relaxed);
}

void ALCVars::endBatch(bool deliverNow)
{
    llassert(on_main_thread() && mBatchDepth > 0);
    mBatchesOpen.fetch_sub(1, std::memory_order_relaxed);
    mFlushWake.notify_one();
    if (--mBatchDepth == 0 && !mInPass && deliverNow)
    {
        drainQueued();
        deliver();
    }
}

ALCVars::Batch::Batch()
:   mStore(ALCVars::instance().serial()),
    mUnwinding(std::uncaught_exceptions())
{
    ALCVars::instance().beginBatch();
}

ALCVars::Batch::~Batch() noexcept(false)
{
    if (!ALCVars::instanceExists() || ALCVars::instance().serial() != mStore)
    {
        return;
    }
    ALCVars::instance().endBatch(std::uncaught_exceptions() == mUnwinding);
}

void ALCVars::pump()
{
    llassert(on_main_thread());
    std::vector<PostedWrite> posted;
    {
        LLCoros::LockType lock(mQueueMutex);
        posted.swap(mPosted);
    }
    for (const PostedWrite& write : posted)
    {
        switch (write.kind)
        {
            case PostedWrite::Kind::Set:          set(write.index, write.value); break;
            case PostedWrite::Kind::Reset:        reset(write.index); break;
            case PostedWrite::Kind::Session:      setSession(write.index, write.value); break;
            case PostedWrite::Kind::ClearSession: clearSession(write.index); break;
        }
    }
    drainQueued();
    if (mBatchDepth == 0 && !mInPass && !mPending.empty())
    {
        deliver();
    }
}

// ---- hooks ----------------------------------------------------------------

void ALCVars::registerValidator(std::string_view name, Validator validator, HookThread thread)
{
    llassert(on_main_thread());
    U32 hook = NONE;
    for (U32 i = 0; i < U32(mValidators.size()); ++i)
    {
        if (mValidators[i].name == name)
        {
            hook = i;
        }
    }
    if (hook == NONE)
    {
        hook = U32(mValidators.size());
        mValidators.push_back({ std::string(name), {}, thread });
    }
    mValidators[hook].fn     = std::move(validator);
    mValidators[hook].thread = thread;
    for (Setting& setting : mSettings)
    {
        if (setting.desc->validate == name)
        {
            setting.validator = hook;
        }
    }
}

bool ALCVars::runValidator(U32 index, ALCVarValue& value)
{
    const U32 hook = mSettings[index].validator;
    if (hook == NONE)
    {
        return true;
    }
    // What the validator adjusts is fitted to the setting again.
    return mValidators[hook].fn(desc(index), value) && validate(index, value);
}

bool ALCVars::postsToMain(U32 index) const
{
    // Validators are registered before any other thread writes, so this
    // reads them with no lock.
    const U32 hook = mSettings[index].validator;
    return hook != NONE && mValidators[hook].thread == HookThread::Main && !on_main_thread();
}

void ALCVars::post(U32 index, PostedWrite::Kind kind, ALCVarValue value)
{
    LLCoros::LockType lock(mQueueMutex);
    mPosted.push_back({ index, kind, std::move(value) });
}

void ALCVars::registerMigration(std::string_view name, ALCVarMigration migration)
{
    llassert(on_main_thread());
    for (auto& [registered, fn] : mMigrations)
    {
        if (registered == name)
        {
            fn = std::move(migration);
            return;
        }
    }
    mMigrations.emplace_back(std::string(name), std::move(migration));
}

const ALCVarMigration* ALCVars::migration(std::string_view name) const
{
    for (const auto& [registered, fn] : mMigrations)
    {
        if (registered == name)
        {
            return &fn;
        }
    }
    return nullptr;
}

void ALCVars::checkHooks() const
{
    std::vector<std::string> missing;
    for (const Setting& setting : mSettings)
    {
        const ALCVarDesc& desc = *setting.desc;
        if (!desc.validate.empty() && setting.validator == NONE)
        {
            missing.push_back(fmt::format("{} (validate {})", desc.name, desc.validate));
        }
        for (const ALCVarMigrate& migrate : desc.migrates)
        {
            if (migrate.kind == ALCVarMigrate::Kind::Hook && !migration(migrate.hook))
            {
                missing.push_back(fmt::format("{} (migrate fn {})", desc.name, migrate.hook));
            }
            for (const ALCVarJoin& join : migrate.join)
            {
                if (join.kind == ALCVarMigrate::Kind::Hook && !migration(join.hook))
                {
                    missing.push_back(fmt::format("{} (join fn {})", desc.name, join.hook));
                }
            }
        }
        for (const ALCVarFrom& from : desc.from)
        {
            if (!migration(from.hook))
            {
                missing.push_back(fmt::format("{} (from {} fn {})", desc.name, from.name, from.hook));
            }
        }
    }
    if (!missing.empty())
    {
        std::string names;
        for (const auto& name : missing)
        {
            names += (names.empty() ? "" : ", ") + name;
        }
        LL_ERRS("ALCVars") << "the schema names hooks nothing registered: " << names << LL_ENDL;
    }
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

        // The store, if the handle's setting is the one it has at this
        // index: a handle's write before registration, or of another schema,
        // does nothing.
        ALCVars* storeOf(const ALCVarDesc& desc, U32 index)
        {
            ALCVars* store = existing();
            if (!store || !store->registered() || index >= store->settingCount() || &store->desc(index) != &desc)
            {
                LL_WARNS_ONCE("ALCVars") << desc.name << " written before the store has its schema" << LL_ENDL;
                return nullptr;
            }
            return store;
        }
    }

    bool set(const ALCVarDesc& desc, U32 index, ALCVarValue value)
    {
        ALCVars* store = storeOf(desc, index);
        return store && store->set(index, value);
    }

    void reset(const ALCVarDesc& desc, U32 index)
    {
        if (ALCVars* store = storeOf(desc, index))
        {
            store->reset(index);
        }
    }

    bool isUserSet(const ALCVarDesc& desc, U32 index)
    {
        ALCVars* store = storeOf(desc, index);
        return store && store->isUserSet(index);
    }

    void resetGroup(const ALCVarGroupDesc& desc, U32 group)
    {
        ALCVars* store = existing();
        if (store && store->registered() && group < store->groupCount() && &store->groupDesc(group) == &desc)
        {
            store->resetGroup(group);
        }
    }
    ALCVarConnection listen(const ALCVarDesc& desc, U32 index, ALCVarListener listener)
    {
        ALCVars* store = storeOf(desc, index);
        return store ? store->listen(index, std::move(listener)) : ALCVarConnection();
    }

    ALCVarConnection listenGroup(const ALCVarGroupDesc& desc, U32 group, ALCVarGroupListener listener)
    {
        ALCVars* store = existing();
        if (store && store->registered() && group < store->groupCount() && &store->groupDesc(group) == &desc)
        {
            return store->listenGroup(group, std::move(listener));
        }
        return {};
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

bool ALCVarConnection::connected() const
{
    return mKind != Kind::None && ALCVars::instanceExists() && ALCVars::instance().connected(mKind, mIndex, mId);
}

void ALCVarConnection::disconnect()
{
    if (mKind != Kind::None && ALCVars::instanceExists())
    {
        ALCVars::instance().disconnect(mKind, mIndex, mId);
    }
    mKind = Kind::None;
}
