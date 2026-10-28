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
// A float's word read into a float register. Clang folds the integer load
// and the cast into one float load. GCC's atomic load on arm64 is an
// integer's, moved across after it (ldr w0, then fmov s0), so GCC reads the
// word as a volatile float, as MSVC does: an aligned word's one load. The
// float type may alias the word, so no store of the word as an integer is
// moved past the read.
#if defined(__clang__)
#define ALCVAR_LOAD_RELAXED_F32(p) __builtin_bit_cast(F32, __atomic_load_n((p), __ATOMIC_RELAXED))
#define ALCVAR_LOAD_RELAXED_F64(p) __builtin_bit_cast(F64, __atomic_load_n((p), __ATOMIC_RELAXED))
#else
namespace ALCVarAtomic
{
    typedef F32 __attribute__((may_alias)) WordF32;
    typedef F64 __attribute__((may_alias)) WordF64;
}
#define ALCVAR_LOAD_RELAXED_F32(p) (*reinterpret_cast<const volatile ALCVarAtomic::WordF32*>(p))
#define ALCVAR_LOAD_RELAXED_F64(p) (*reinterpret_cast<const volatile ALCVarAtomic::WordF64*>(p))
#endif
// A bool's byte, in the form each compiler reads in one load: Clang masks a
// byte cast to a bool to its low bit, and takes a bool's own load as 0 or 1;
// GCC tests a bool's atomic load against zero, and takes the cast byte as it
// is. The byte is written only as a char type, which every access may alias.
#if defined(__clang__)
#define ALCVAR_LOAD_RELAXED_BOOL(p) __atomic_load_n(reinterpret_cast<const bool*>(p), __ATOMIC_RELAXED)
#else
#define ALCVAR_LOAD_RELAXED_BOOL(p) __builtin_bit_cast(bool, __atomic_load_n((p), __ATOMIC_RELAXED))
#endif
#else
#include <intrin.h>
#define ALCVAR_GNU_ATOMICS 0
#define ALCVAR_LOAD_RELAXED_8(p)  static_cast<U8>(__iso_volatile_load8(reinterpret_cast<const volatile char*>(p)))
#define ALCVAR_LOAD_RELAXED_32(p) static_cast<U32>(__iso_volatile_load32(reinterpret_cast<const volatile int*>(p)))
#define ALCVAR_LOAD_RELAXED_64(p) \
    static_cast<U64>(__iso_volatile_load64(reinterpret_cast<const volatile __int64*>(p)))
// A float's word read as a float, into a float register: the integer
// intrinsic would load it into an integer register and move it across. An
// aligned volatile access is one instruction, as the intrinsic's is, and MSVC
// has no type-based aliasing for it to break.
#define ALCVAR_LOAD_RELAXED_F32(p) (*reinterpret_cast<const volatile F32*>(p))
#define ALCVAR_LOAD_RELAXED_F64(p) (*reinterpret_cast<const volatile F64*>(p))
#define ALCVAR_LOAD_RELAXED_BOOL(p) __builtin_bit_cast(bool, ALCVAR_LOAD_RELAXED_8(p))
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
