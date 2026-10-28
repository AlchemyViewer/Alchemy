/**
 * @file alcvar.h
 * @brief The handles a setting is read and written through: an empty object
 *        whose type says where the setting's slot is.
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

#include "alcvaratomic.h"
#include "alcvarconnection.h"
#include "alcvardesc.h"
#include "alcvarvalue.h"

#include "llerror.h"

#include <boost/smart_ptr/shared_ptr.hpp>

#include <span>
#include <string>
#include <string_view>

extern LL_COMMON_API bool on_main_thread();

// What the handles ask of the store, by a setting's index; ALCVars answers.
namespace ALCVarAccess
{
    bool set(const ALCVarDesc& desc, U32 index, ALCVarValue value);
    void reset(const ALCVarDesc& desc, U32 index);
    bool isUserSet(const ALCVarDesc& desc, U32 index);
    U32  generation(U32 index);
    ALCVarConnection listen(const ALCVarDesc& desc, U32 index, ALCVarListener listener);

    U64  groupGeneration(U32 group);
    U64  tagGeneration(U32 tag);
    void resetGroup(const ALCVarGroupDesc& desc, U32 group);
    ALCVarConnection listenGroup(const ALCVarGroupDesc& desc, U32 group, ALCVarGroupListener listener);

    // A string's or a list's compiled default, for a slot nothing has
    // published into yet: made the first time it is asked for, and kept.
    boost::shared_ptr<const void> sharedDefault(const ALCVarDesc& desc);
}

namespace ALCVarDetail
{
    // The words of a value behind a sequence word, as they were at one
    // moment: read again if a write was under way or came between.
    template <size_t N>
    LL_FORCE_INLINE void readWords(const ALCVarSeqSlot<N>& slot, U64* out)
    {
        for (;;)
        {
            const U64 before = ALCVarAtomic::loadAcquire(&slot.sequence);
            for (size_t i = 0; i < N; ++i)
            {
                out[i] = ALCVarAtomic::loadRelaxed(&slot.words[i]);
            }
            ALCVarAtomic::fenceAcquire();
            if (!(before & 1) && ALCVarAtomic::loadRelaxed(&slot.sequence) == before)
            {
                return;
            }
        }
    }
}

// A setting's handle: `cvar::render::shadows::resolution_scale`. The type
// fixes the slot -- the group's storage G and the setting's place in it --
// so a read is a load from one address the linker gives, with no object to
// look into.
//
//   F32 scale = shadows::resolution_scale();   // any thread
//   shadows::resolution_scale.set(2.f);        // any thread; kept on disk soon after
//   shadows::resolution_scale.reset();         // drops the user's value
//   auto connection = shadows::resolution_scale.onChange([](F32 value, F32 old) { ... });
template <typename T, typename G, U16 KEY>
class ALCVar
{
public:
    using value_type = T;
    using traits     = ALCVarTraits<T>;

    static constexpr ALCVarStorage storage = traits::storage;
    static constexpr U16           key     = KEY;

    // The value, from any thread: one load for a number or a bool; the words
    // of a small value as they were at one moment; a copy of a string or a
    // list; LLSD on the main thread only.
    LL_FORCE_INLINE T operator()() const noexcept
    {
        constexpr U16 slot = G::slot[KEY];
        if constexpr (storage == ALCVarStorage::Byte)
        {
            // The byte is 0 or 1, as a bool's own is: read as one.
            return ALCVAR_LOAD_RELAXED_BOOL(&G::bytes[slot]);
        }
        else if constexpr (std::is_same_v<T, F32>)
        {
            return ALCVAR_LOAD_RELAXED_F32(&G::words[slot]);
        }
        else if constexpr (std::is_same_v<T, F64>)
        {
            return ALCVAR_LOAD_RELAXED_F64(&G::dwords[slot]);
        }
        else if constexpr (storage == ALCVarStorage::Word)
        {
            return traits::decode(ALCVAR_LOAD_RELAXED_32(&G::words[slot]));
        }
        else if constexpr (storage == ALCVarStorage::DWord)
        {
            return traits::decode(ALCVAR_LOAD_RELAXED_64(&G::dwords[slot]));
        }
        else if constexpr (storage == ALCVarStorage::Pair)
        {
            U64 words[2];
            ALCVarDetail::readWords(G::pairs[slot], words);
            return traits::decode(words);
        }
        else if constexpr (storage == ALCVarStorage::Triple)
        {
            U64 words[3];
            ALCVarDetail::readWords(G::triples[slot], words);
            return traits::decode(words);
        }
        else if constexpr (storage == ALCVarStorage::Shared)
        {
            return *share();
        }
        else
        {
            llassert(on_main_thread());
            const LLSD* value = G::boxed[slot].value;
            return value ? *value : LLSD();
        }
    }

    // For a group's read(), inside its sequence: the words without the
    // value's own sequence, which the group's covers.
    LL_FORCE_INLINE T inGroup() const noexcept
        requires(storage != ALCVarStorage::Shared && storage != ALCVarStorage::Boxed)
    {
        constexpr U16 slot = G::slot[KEY];
        if constexpr (storage == ALCVarStorage::Pair)
        {
            const U64 words[2] = { ALCVarAtomic::loadRelaxed(&G::pairs[slot].words[0]),
                                   ALCVarAtomic::loadRelaxed(&G::pairs[slot].words[1]) };
            return traits::decode(words);
        }
        else if constexpr (storage == ALCVarStorage::Triple)
        {
            const U64 words[3] = { ALCVarAtomic::loadRelaxed(&G::triples[slot].words[0]),
                                   ALCVarAtomic::loadRelaxed(&G::triples[slot].words[1]),
                                   ALCVarAtomic::loadRelaxed(&G::triples[slot].words[2]) };
            return traits::decode(words);
        }
        else
        {
            return (*this)();
        }
    }

    // A string or a list as it is now, kept for as long as the pointer is:
    // for a thread that reads it across a frame's end.
    boost::shared_ptr<const T> share() const
        requires(storage == ALCVarStorage::Shared)
    {
        constexpr U16 slot  = G::slot[KEY];
        boost::shared_ptr<const void> value = G::shared[slot].value.load();
        if (!value)
        {
            value = ALCVarAccess::sharedDefault(desc());
        }
        return boost::static_pointer_cast<const T>(value);
    }

    // On the main thread: the string or list itself, with no copy and no
    // reference counted, valid until the end of the frame. A write from any
    // thread leaves the value it replaces for the main thread to free then.
    auto view() const
        requires(storage == ALCVarStorage::Shared)
    {
        // Checked in the debug configurations alone: the check costs more
        // than the read.
#if LL_DEBUG
        llassert(on_main_thread());
#endif
        constexpr U16 slot    = G::slot[KEY];
        const void*   current = ALCVarAtomic::loadAcquire(&G::shared[slot].current);
        if (!current)
        {
            current = ALCVarAccess::sharedDefault(desc()).get();
        }
        if constexpr (traits::type == ALCVarType::String)
        {
            return std::string_view(*static_cast<const std::string*>(current));
        }
        else
        {
            using element = typename traits::element_type;
            return std::span<const element>(*static_cast<const std::vector<element>*>(current));
        }
    }

    // The user's value, from any thread: false when refused -- out of the
    // choices, failing its validator, or denied by a restriction. A value
    // past the range is clamped into it.
    bool set(const T& value) const
        requires(traits::type != ALCVarType::String)
    {
        return ALCVarAccess::set(desc(), index(), ALCVarValue(value));
    }
    bool set(std::string_view value) const
        requires(traits::type == ALCVarType::String)
    {
        return ALCVarAccess::set(desc(), index(), ALCVarValue(value));
    }

    // Drops the user's value: the layer below it shows.
    void reset() const { ALCVarAccess::reset(desc(), index()); }
    bool isUserSet() const { return ALCVarAccess::isUserSet(desc(), index()); }

    // Called on the main thread with the new value and the old after each
    // change, until the connection goes.
    template <typename F>
    [[nodiscard]] ALCVarConnection onChange(F&& fn) const
    {
        return ALCVarAccess::listen(desc(), index(),
                                    [fn = std::forward<F>(fn)](const ALCVarValue& value, const ALCVarValue& old) {
                                        fn(value.get<T>(), old.get<T>());
                                    });
    }

    // Moves with every change of the effective value.
    U32 generation() const { return ALCVarAccess::generation(index()); }

    static const ALCVarDesc& desc() { return G::desc[KEY]; }
    static std::string_view  name() { return G::desc[KEY].name; }
    static U32               index() { return ALCVarAtomic::loadRelaxed(&G::state.first) + KEY; }
};

// A group's handle: `cvar::render::shadows::group`.
template <typename G>
class ALCVarGroup
{
public:
    static const ALCVarGroupDesc& desc() { return G::info; }
    static U32                    index() { return ALCVarAtomic::loadRelaxed(&G::state.index); }

    // Moves with every change anywhere in the group or below it.
    U64 generation() const { return ALCVarAccess::groupGeneration(index()); }

    // Drops the user's value of every setting in the group and below it.
    void reset() const { ALCVarAccess::resetGroup(desc(), index()); }

    // Called on the main thread with the setting that changed, for a change
    // anywhere in the group or below it.
    template <typename F>
    [[nodiscard]] ALCVarConnection onChange(F&& fn) const
    {
        return ALCVarAccess::listenGroup(desc(), index(), std::forward<F>(fn));
    }
};

// A group's read(): its sequence before the values and after them, the same
// and even when no write came between.
template <typename G>
class ALCVarGroupReader
{
public:
    LL_FORCE_INLINE void begin()
    {
        do
        {
            mSequence = ALCVarAtomic::loadAcquire(&G::state.sequence);
        } while (mSequence & 1);
    }

    LL_FORCE_INLINE bool end() const
    {
        ALCVarAtomic::fenceAcquire();
        return ALCVarAtomic::loadRelaxed(&G::state.sequence) == mSequence;
    }

private:
    U32 mSequence = 0;
};

// A tag's handle: `cvar::tags::shader_input`. Its generation moves with a
// change of any setting that carries the tag, wherever its group is.
template <U32 TAG>
class ALCVarTag
{
public:
    static constexpr U32 index = TAG;

    U64 generation() const { return ALCVarAccess::tagGeneration(TAG); }
};
