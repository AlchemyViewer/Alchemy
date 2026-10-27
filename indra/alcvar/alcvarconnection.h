/**
 * @file alcvarconnection.h
 * @brief Listeners to settings, and the connections that keep them.
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

#include "stdtypes.h"

#include <memory>
#include <type_traits>
#include <utility>

class ALCVarValue;
struct ALCVarDesc;

// A callable that may be moved but not copied, so that a listener can own
// what it captures.
template <typename Signature>
class ALCVarFunction;

template <typename R, typename... Args>
class ALCVarFunction<R(Args...)>
{
public:
    ALCVarFunction() = default;

    template <typename F>
        requires(!std::is_same_v<std::remove_cvref_t<F>, ALCVarFunction> && std::is_invocable_r_v<R, F&, Args...>)
    ALCVarFunction(F&& fn) : mCallable(std::make_unique<Callable<std::decay_t<F>>>(std::forward<F>(fn)))
    {
    }

    explicit operator bool() const { return mCallable != nullptr; }
    R operator()(Args... args) const { return mCallable->call(std::forward<Args>(args)...); }

private:
    struct Base
    {
        virtual ~Base()                = default;
        virtual R call(Args... args) = 0;
    };

    template <typename F>
    struct Callable final : Base
    {
        explicit Callable(F&& fn) : mFn(std::move(fn)) {}
        explicit Callable(const F& fn) : mFn(fn) {}
        R call(Args... args) override { return mFn(std::forward<Args>(args)...); }
        F mFn;
    };

    std::unique_ptr<Base> mCallable;
};

// Told of a setting's change, on the main thread: its new value and the last
// one listeners were given.
using ALCVarListener = ALCVarFunction<void(const ALCVarValue& value, const ALCVarValue& old)>;
// Told of a change anywhere in a group or the groups below it.
using ALCVarGroupListener = ALCVarFunction<void(const ALCVarDesc& changed)>;

// A listener's connection: the listener is disconnected when it goes. A
// listener disconnected while listeners are being told of a change -- by
// itself or by another -- is not called again, and is removed once they
// all have been.
class ALCVarConnection
{
public:
    enum class Kind : U8
    {
        None,
        Setting,
        Group,
    };

    ALCVarConnection() = default;
    ALCVarConnection(Kind kind, U32 index, U64 id) : mKind(kind), mIndex(index), mId(id) {}
    ~ALCVarConnection() { disconnect(); }

    ALCVarConnection(ALCVarConnection&& other) noexcept
    :   mKind(std::exchange(other.mKind, Kind::None)),
        mIndex(other.mIndex),
        mId(other.mId)
    {
    }

    ALCVarConnection& operator=(ALCVarConnection&& other) noexcept
    {
        if (this != &other)
        {
            disconnect();
            mKind  = std::exchange(other.mKind, Kind::None);
            mIndex = other.mIndex;
            mId    = other.mId;
        }
        return *this;
    }

    ALCVarConnection(const ALCVarConnection&)            = delete;
    ALCVarConnection& operator=(const ALCVarConnection&) = delete;

    bool connected() const;
    void disconnect();
    // Keeps the listener for as long as the store lives, without this.
    void release() { mKind = Kind::None; }

private:
    Kind mKind  = Kind::None;
    U32  mIndex = 0;
    U64  mId    = 0;
};
