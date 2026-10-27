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

#include "llfile.h"

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

void ALCVars::apply(std::span<const U32> indices)
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
    // long way rather than letting a stale flag pass a fresh value.
    const U32 sequence = mWriteSeq[index].load(std::memory_order_acquire);
    const U8  flags    = mFlags[index].load(std::memory_order_relaxed);
    if (flags & FLAG_DENIED)
    {
        return false;
    }
    if (!(sequence & 1) && unchanged(index, input, flags))
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
        layer(user).erase(index);
    }
    else
    {
        layer(user).insert_or_assign(index, std::move(value));
    }
    const U32 one[] = { index };
    apply(one);
    return true;
}

void ALCVars::reset(U32 index)
{
    auto lock = lockWrites();
    if (restricted(mDenied, index))
    {
        return;
    }
    layer(userLayer(index)).erase(index);
    const U32 one[] = { index };
    apply(one);
}

void ALCVars::resetGroup(U32 group)
{
    auto             lock = lockWrites();
    std::vector<U32> touched;
    for (U32 i = 0; i < U32(mSettings.size()); ++i)
    {
        if (inGroup(i, group))
        {
            if (restricted(mDenied, i))
            {
                continue;
            }
            layer(userLayer(i)).erase(i);
            touched.push_back(i);
        }
    }
    apply(touched);
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
    auto lock = lockWrites();
    layer(ALCVarLayer::Session).insert_or_assign(index, std::move(value));
    const U32 one[] = { index };
    apply(one);
    return true;
}

void ALCVars::clearSession(U32 index)
{
    auto lock = lockWrites();
    layer(ALCVarLayer::Session).erase(index);
    const U32 one[] = { index };
    apply(one);
}

void ALCVars::setQuality(std::span<const std::pair<U32, ALCVarValue>> values, bool dropUser)
{
    auto             lock = lockWrites();
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
            layer(ALCVarLayer::Global).erase(index);
            layer(ALCVarLayer::Account).erase(index);
        }
        if (std::find(touched.begin(), touched.end(), index) == touched.end())
        {
            touched.push_back(index);
        }
    }
    layer(ALCVarLayer::Quality) = std::move(next);
    apply(touched);
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
    auto  lock  = lockWrites();
    auto& holds = mHolds[index];
    std::erase_if(holds, [owner](const Hold& hold) { return hold.owner == owner; });
    holds.push_back({ std::string(owner), std::move(held) });
    const U32 one[] = { index };
    apply(one);
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
    auto  lock   = lockWrites();
    auto& clamps = mClamps[index];
    std::erase_if(clamps, [owner](const Clamp& clamp) { return clamp.owner == owner; });
    clamps.push_back({ std::string(owner), lowest, highest });
    const U32 one[] = { index };
    apply(one);
}

void ALCVars::release(std::string_view owner, U32 index)
{
    auto lock = lockWrites();
    if (const auto holds = mHolds.find(index); holds != mHolds.end())
    {
        std::erase_if(holds->second, [owner](const Hold& hold) { return hold.owner == owner; });
    }
    if (const auto clamps = mClamps.find(index); clamps != mClamps.end())
    {
        std::erase_if(clamps->second, [owner](const Clamp& clamp) { return clamp.owner == owner; });
    }
    const U32 one[] = { index };
    apply(one);
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
    auto             lock = lockWrites();
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
    apply(touched);
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
