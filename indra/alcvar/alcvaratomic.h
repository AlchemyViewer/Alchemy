/**
 * @file alcvaratomic.h
 * @brief Atomic access to the plain words settings' slots are made of.
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

#include "llpreprocessor.h"
#include "stdtypes.h"

#include <atomic>

// A slot is a plain integer, constant-initialized with its compiled default,
// and every access to it is atomic, as std::atomic_ref makes one. The
// relaxed loads a handle reads through are the compiler's own builtins,
// which stay one instruction even where nothing is inlined, as in a debug
// build: a handle's read is one load in every configuration.
#if defined(__GNUC__) || defined(__clang__)
#define ALCVAR_GNU_ATOMICS 1
#define ALCVAR_LOAD_RELAXED_8(p)  __atomic_load_n((p), __ATOMIC_RELAXED)
#define ALCVAR_LOAD_RELAXED_32(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#define ALCVAR_LOAD_RELAXED_64(p) __atomic_load_n((p), __ATOMIC_RELAXED)
#else
#include <intrin.h>
#define ALCVAR_GNU_ATOMICS 0
#define ALCVAR_LOAD_RELAXED_8(p)  static_cast<U8>(__iso_volatile_load8(reinterpret_cast<const volatile char*>(p)))
#define ALCVAR_LOAD_RELAXED_32(p) static_cast<U32>(__iso_volatile_load32(reinterpret_cast<const volatile int*>(p)))
#define ALCVAR_LOAD_RELAXED_64(p) \
    static_cast<U64>(__iso_volatile_load64(reinterpret_cast<const volatile __int64*>(p)))
#endif

namespace ALCVarAtomic
{
    template <typename T>
    LL_FORCE_INLINE T loadRelaxed(const T* p)
    {
#if ALCVAR_GNU_ATOMICS
        return __atomic_load_n(p, __ATOMIC_RELAXED);
#else
        return std::atomic_ref<T>(*const_cast<T*>(p)).load(std::memory_order_relaxed);
#endif
    }

    template <typename T>
    LL_FORCE_INLINE T loadAcquire(const T* p)
    {
#if ALCVAR_GNU_ATOMICS
        return __atomic_load_n(p, __ATOMIC_ACQUIRE);
#else
        return std::atomic_ref<T>(*const_cast<T*>(p)).load(std::memory_order_acquire);
#endif
    }

    template <typename T>
    LL_FORCE_INLINE void storeRelaxed(T* p, T value)
    {
#if ALCVAR_GNU_ATOMICS
        __atomic_store_n(p, value, __ATOMIC_RELAXED);
#else
        std::atomic_ref<T>(*p).store(value, std::memory_order_relaxed);
#endif
    }

    template <typename T>
    LL_FORCE_INLINE void storeRelease(T* p, T value)
    {
#if ALCVAR_GNU_ATOMICS
        __atomic_store_n(p, value, __ATOMIC_RELEASE);
#else
        std::atomic_ref<T>(*p).store(value, std::memory_order_release);
#endif
    }

    template <typename T>
    LL_FORCE_INLINE T fetchAdd(T* p, T value)
    {
#if ALCVAR_GNU_ATOMICS
        return __atomic_fetch_add(p, value, __ATOMIC_RELAXED);
#else
        return std::atomic_ref<T>(*p).fetch_add(value, std::memory_order_relaxed);
#endif
    }

    LL_FORCE_INLINE void fenceAcquire() { std::atomic_thread_fence(std::memory_order_acquire); }
    LL_FORCE_INLINE void fenceRelease() { std::atomic_thread_fence(std::memory_order_release); }
}
