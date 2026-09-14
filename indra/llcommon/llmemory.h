/**
 * @file llmemory.h
 * @brief Memory allocation/deallocation header-stuff goes here.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */
#ifndef LLMEMORY_H
#define LLMEMORY_H

#include "linden_common.h"
#include "llframetimer.h"
#include "llunits.h"
#include "stdtypes.h"
#if !LL_WINDOWS
#include <stdint.h>
#endif

class LLMutex ;

#if LL_WINDOWS && LL_DEBUG
#define LL_CHECK_MEMORY llassert(_CrtCheckMemory());
#else
#define LL_CHECK_MEMORY
#endif


#if LL_WINDOWS
#define LL_ALIGN_OF __alignof
#else
#define LL_ALIGN_OF __align_of__
#endif

#if defined(LL_X86_64) || defined(LL_ARM64)
#define LL_DEFAULT_HEAP_ALIGN 16
#else
#if LL_WINDOWS
#define LL_DEFAULT_HEAP_ALIGN 8
#elif LL_DARWIN
#define LL_DEFAULT_HEAP_ALIGN 16
#elif LL_LINUX
#define LL_DEFAULT_HEAP_ALIGN 8
#endif
#endif

LL_COMMON_API void ll_assert_aligned_func(uintptr_t ptr,U32 alignment);

#ifdef SHOW_ASSERT
// This is incredibly expensive - in profiling Windows RWD builds, 30%
// of CPU time was in aligment checks.
//#define ASSERT_ALIGNMENT
#endif

#ifdef ASSERT_ALIGNMENT
#define ll_assert_aligned(ptr,alignment) ll_assert_aligned_func(uintptr_t(ptr),((U32)alignment))
#else
#define ll_assert_aligned(ptr,alignment)
#endif

#include "alsimd.h"

template <typename T> T* LL_NEXT_ALIGNED_ADDRESS(T* address)
{
    return reinterpret_cast<T*>(
        (uintptr_t(address) + 0xF) & ~0xF);
}

template <typename T> T* LL_NEXT_ALIGNED_ADDRESS_64(T* address)
{
    return reinterpret_cast<T*>(
        (uintptr_t(address) + 0x3F) & ~0x3F);
}

#define LL_ALIGN_NEW                        \
public:                                     \
    void* operator new(size_t size)         \
    {                                       \
        return ll_aligned_malloc_16(size);  \
    }                                       \
                                            \
    void operator delete(void* ptr)         \
    {                                       \
        ll_aligned_free_16(ptr);            \
    }                                       \
                                            \
    void* operator new[](size_t size)       \
    {                                       \
        return ll_aligned_malloc_16(size);  \
    }                                       \
                                            \
    void operator delete[](void* ptr)       \
    {                                       \
        ll_aligned_free_16(ptr);            \
    }

//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
    // for enable buffer overrun detection predefine LL_DEBUG_BUFFER_OVERRUN in current library
    // change preprocessor code to: #if 1 && defined(LL_WINDOWS)

#if 0 && defined(LL_WINDOWS)
    void* ll_aligned_malloc_fallback( size_t size, int align );
    void ll_aligned_free_fallback( void* ptr );
//------------------------------------------------------------------------------------------------
#else
    inline void* ll_aligned_malloc_fallback( size_t size, size_t align )
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
    #if defined(LL_WINDOWS)
        void* ret = _aligned_malloc(size, align);
    #elif defined(LL_LINUX)
        void *ret;
        if (0 != posix_memalign(&ret, align, size))
            return nullptr;
    #else
        char* aligned = NULL;
        void* mem = malloc( size + (align - 1) + sizeof(void*) );
        if (mem)
        {
            aligned = ((char*)mem) + sizeof(void*);
            aligned += align - ((uintptr_t)aligned & (align - 1));

            ((void**)aligned)[-1] = mem;
        }
        void* ret = aligned;
    #endif
        LL_PROFILE_ALLOC(ret, size);
        return ret;
    }

    inline void ll_aligned_free_fallback( void* ptr )
    {
        LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
        LL_PROFILE_FREE(ptr);
    #if defined(LL_WINDOWS)
        _aligned_free(ptr);
    #elif defined(LL_LINUX)
        free(ptr);
    #else
        if (ptr)
        {
            free( ((void**)ptr)[-1] );
        }
    #endif
    }
#endif
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------

inline void* ll_aligned_malloc_16(size_t size) // returned hunk MUST be freed with ll_aligned_free_16().
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
#if LL_DEFAULT_HEAP_ALIGN == 16
    void* ret = malloc(size); // default osx and 64-bit malloc is 16 byte aligned.
#elif defined(LL_WINDOWS)
    void* ret = _aligned_malloc(size, 16);
#else
    void *ret;
    if (0 != posix_memalign(&ret, 16, size))
        return nullptr;
#endif
    LL_PROFILE_ALLOC(ret, size);
    return ret;
}

inline void ll_aligned_free_16(void *p)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
    LL_PROFILE_FREE(p);
#if LL_DEFAULT_HEAP_ALIGN == 16
    free(p);
#elif defined(LL_WINDOWS)
    _aligned_free(p);
#else
    free(p); // posix_memalign() is compatible with heap deallocator
#endif
}

inline void* ll_aligned_realloc_16(void* ptr, size_t size, size_t old_size) // returned hunk MUST be freed with ll_aligned_free_16().
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
    LL_PROFILE_FREE(ptr);
#if LL_DEFAULT_HEAP_ALIGN == 16
    void* ret = realloc(ptr,size); // default osx and 64bit malloc is 16 byte aligned.
#elif defined(LL_WINDOWS)
    void* ret = _aligned_realloc(ptr, size, 16);
#else
    //FIXME: memcpy is SLOW
    void* ret = ll_aligned_malloc_16(size);
    if (ptr)
    {
        if (ret)
        {
            // Only copy the size of the smallest memory block to avoid memory corruption.
            memcpy(ret, ptr, llmin(old_size, size));
        }
        ll_aligned_free_16(ptr);
    }
#endif
    LL_PROFILE_ALLOC(ret, size);
    return ret;
}

inline void* ll_aligned_malloc_32(size_t size) // returned hunk MUST be freed with ll_aligned_free_32().
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
#if defined(LL_WINDOWS)
    void* ret = _aligned_malloc(size, 32);
#else
    void *ret;
    if (0 != posix_memalign(&ret, 32, size))
        return nullptr;
#endif
    LL_PROFILE_ALLOC(ret, size);
    return ret;
}

inline void ll_aligned_free_32(void *p)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
    LL_PROFILE_FREE(p);
#if defined(LL_WINDOWS)
    _aligned_free(p);
#else
    free(p); // posix_memalign() is compatible with heap deallocator
#endif
}

inline void* ll_aligned_malloc_64(size_t size) // returned hunk MUST be freed with ll_aligned_free_32().
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
#if defined(LL_WINDOWS)
    void* ret = _aligned_malloc(size, 64);
#else
    void *ret;
    if (0 != posix_memalign(&ret, 64, size))
        return nullptr;
#endif
    LL_PROFILE_ALLOC(ret, size);
    return ret;
}

inline void ll_aligned_free_64(void *p)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
    LL_PROFILE_FREE(p);
#if defined(LL_WINDOWS)
    _aligned_free(p);
#else
    free(p); // posix_memalign() is compatible with heap deallocator
#endif
}

// general purpose dispatch functions that are forced inline so they can compile down to a single call
template<size_t ALIGNMENT>
LL_FORCE_INLINE void* ll_aligned_malloc(size_t size)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
    void* ret;
    if constexpr (LL_DEFAULT_HEAP_ALIGN % ALIGNMENT == 0)
    {
        ret = malloc(size);
        LL_PROFILE_ALLOC(ret, size);
    }
    else if constexpr (ALIGNMENT == 16)
    {
        ret = ll_aligned_malloc_16(size);
    }
    else if constexpr (ALIGNMENT == 32)
    {
        ret = ll_aligned_malloc_32(size);
    }
    else if constexpr (ALIGNMENT == 64)
    {
        ret = ll_aligned_malloc_64(size);
    }
    else
    {
        ret = ll_aligned_malloc_fallback(size, ALIGNMENT);
    }
    return ret;
}

template<size_t ALIGNMENT>
LL_FORCE_INLINE void ll_aligned_free(void* ptr)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
    if constexpr (ALIGNMENT == LL_DEFAULT_HEAP_ALIGN)
    {
        LL_PROFILE_FREE(ptr);
        free(ptr);
    }
    else if constexpr (ALIGNMENT == 16)
    {
        ll_aligned_free_16(ptr);
    }
    else if constexpr (ALIGNMENT == 32)
    {
        return ll_aligned_free_32(ptr);
    }
    else if constexpr (ALIGNMENT == 64)
    {
        return ll_aligned_free_32(ptr);
    }
    else
    {
        return ll_aligned_free_fallback(ptr);
    }
}

// Copy words 16-byte blocks from src to dst. Source and destination MUST NOT OVERLAP.
// Source and dest must be 16-byte aligned and size must be multiple of 16.
//
inline void ll_memcpy_nonaliased_aligned_16(char* __restrict dst, const char* __restrict src, size_t bytes)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_MEMORY;
#if defined(LL_ARM64)
    memcpy(dst, src, bytes);
#else
    assert(src != NULL);
    assert(dst != NULL);
    assert(bytes > 0);
    assert((bytes % sizeof(F32))== 0);
    ll_assert_aligned(src,16);
    ll_assert_aligned(dst,16);

    assert((src < dst) ? ((src + bytes) <= dst) : ((dst + bytes) <= src));
    assert(bytes%16==0);

    char* end = dst + bytes;

    if (bytes > 64)
    {

        // Find start of 64b aligned area within block
        //
        char* begin_64 = LL_NEXT_ALIGNED_ADDRESS_64(dst);

        //at least 64 bytes before the end of the destination, switch to 16 byte copies
        char* end_64 = end-64;

        // Prefetch the head of the 64b area now
        //
        alsimd::prefetch_nta(begin_64);
        alsimd::prefetch_nta(begin_64 + 64);
        alsimd::prefetch_nta(begin_64 + 128);
        alsimd::prefetch_nta(begin_64 + 192);

        // Copy 16b chunks until we're 64b aligned
        //
        while (dst < begin_64)
        {

            alsimd::store((F32*)dst, alsimd::load((F32*)src));
            dst += 16;
            src += 16;
        }

        // Copy 64b chunks up to your tail
        //
        // might be good to shmoo the 512b prefetch offset
        // (characterize performance for various values)
        //
        while (dst < end_64)
        {
            alsimd::prefetch_nta(src + 512);
            alsimd::prefetch_nta(dst + 512);
            alsimd::store((F32*)dst, alsimd::load((F32*)src));
            alsimd::store((F32*)(dst + 16), alsimd::load((F32*)(src + 16)));
            alsimd::store((F32*)(dst + 32), alsimd::load((F32*)(src + 32)));
            alsimd::store((F32*)(dst + 48), alsimd::load((F32*)(src + 48)));
            dst += 64;
            src += 64;
        }
    }

    // Copy remainder 16b tail chunks (or ALL 16b chunks for sub-64b copies)
    //
    while (dst < end)
    {
        alsimd::store((F32*)dst, alsimd::load((F32*)src));
        dst += 16;
        src += 16;
    }
#endif
}

#ifndef __DEBUG_PRIVATE_MEM__
#define __DEBUG_PRIVATE_MEM__  0
#endif

class LL_COMMON_API LLMemory
{
public:
    // Return the resident set size of the current process, in bytes.
    // Return value is zero if not known.
    static U64 getCurrentRSS();
    static void* tryToAlloc(void* address, U32 size);
    static void initMaxHeapSizeGB(F32Gigabytes max_heap_size);
    static void updateMemoryInfo() ;
    // Refreshes the memory counters at most once per second. Every subsystem
    // that polls free memory should call this rather than updateMemoryInfo(),
    // so the whole viewer shares a single sample per second.
    static void updateFreeSystemMemory();
    static void logMemoryInfo(bool update = false);

    // Scales down draw distance as free system memory runs out. Returns 1 for
    // no reduction, up to 2 for half range.
    static F32 getSystemMemoryBudgetFactor();
    // Machines that will never exhaust system memory can opt out entirely,
    // pinning the factor at 1 so draw distance is left alone.
    static void setSystemMemoryBudgetEnabled(bool enabled);

#if LL_WINDOWS
    // Commit charge is a Windows-only concept, combines page file and ram
    static U32Megabytes getAvailableCommitMemMB();
#endif
    static U32Kilobytes getAvailableMemKB() ;
    // The free memory that runs out first: physical, or on Windows the commit
    // charge when that is the scarcer. Everything that acts on memory pressure
    // reads this one figure, so the texture bias and the draw-distance factor
    // escalate in the order they were designed to.
    static S32Megabytes getScarcestFreeMemMB();
    static U32Kilobytes getMaxMemKB() ;
    static U32Kilobytes getMaxHeapSizeKB() { return sMaxHeapSizeInKB; }
    static U32Kilobytes getAllocatedMemKB() ;
private:
    // LLMemoryInfo directly updates memory stats
    friend class LLMemoryInfo;

    static U32Megabytes sAvailCommitMemInMB;
    static U32Kilobytes sAvailPhysicalMemInKB ;
    static U32Kilobytes sMaxPhysicalMemInKB ;
    static U32Kilobytes sAllocatedMemInKB;
    static U32Kilobytes sAllocatedPageSizeInKB ;

    static U32Kilobytes sMaxHeapSizeInKB;

    static LLFrameTimer sMemoryCheckTimer;
    static F32 sSysMemoryFactor;
    static U32 sFactorLastFrameCount;
    static bool sSysMemoryBudgetEnabled;
};

// LLRefCount moved to llrefcount.h

// LLPointer moved to llpointer.h

// LLSafeHandle moved to llsafehandle.h

// LLSingleton moved to llsingleton.h




#endif
