/**
 * @file algpubuffer.h
 * @brief RAII wrapper around a GL buffer object the GPU reads and writes: shader storage, indirect commands, readback,
 *        and the rings that read results back and stream data in without waiting on the GPU.
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

#include <cstddef>
#include <vector>

// GL 4.3's minimum GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, so every point here exists on every context that has
// compute. LLRender keeps what each holds; the reserved storage blocks' fixed points all fall below it.
constexpr U32 AL_NUM_STORAGE_BINDINGS = 8;

// What glDrawArraysIndirect reads: GL's DrawArraysIndirectCommand, word for word. A shader that writes one declares
// the same four uints at the head of its storage block. mBaseInstance offsets only instanced vertex attributes, which a
// draw with no vertex arrays has none of; below GL 4.2 the word is reserved and must be 0.
struct ALDrawArraysIndirectCommand
{
    U32 mCount         = 0;
    U32 mInstanceCount = 0;
    U32 mFirst         = 0;
    U32 mBaseInstance  = 0;
};
static_assert(sizeof(ALDrawArraysIndirectCommand) == 16, "GL's DrawArraysIndirectCommand is four uints");

// What glDispatchComputeIndirect reads: the work group counts, x, y, z.
struct ALDispatchIndirectCommand
{
    U32 mGroupsX = 0;
    U32 mGroupsY = 0;
    U32 mGroupsZ = 0;
};
static_assert(sizeof(ALDispatchIndirectCommand) == 12, "GL's DispatchIndirectCommand is three uints");

// A GL buffer object the GPU reads and writes: a shader storage block's store, the commands an indirect draw or
// dispatch reads, a place to read results back from. Where ALUniformBuffer streams constants the CPU rewrites every
// frame, this holds data that lives on the GPU: allocated once at a size, then written by shaders, cleared and copied
// GPU-side, and uploaded to only where it was allocated DYNAMIC or STREAM.
//
// The store is immutable (glBufferStorage) where that resolved and mutable (glBufferData) elsewhere; either way it is
// replaced only by allocate(). Calls are DSA (glNamed*) where gGLManager.mHasDirectStateAccess says so; the path
// without binds only GL_COPY_WRITE_BUFFER and GL_COPY_READ_BUFFER, which nothing else in the renderer caches, so no
// other binding cache -- LLVertexBuffer's, LLRender's indirect and storage ones -- can go stale behind it. Binding it
// for a shader or a draw is LLRender's: bindStorageBuffer, drawArraysIndirect, dispatchComputeIndirect.
//
// Shader writes reach what reads the buffer next only past the barrier for that kind of read: LLRender::memoryBarrier
// with BARRIER_STORAGE for a later shader, BARRIER_COMMAND for an indirect draw or dispatch, BARRIER_BUFFER_UPDATE for
// upload, clear and copy. read() makes its own.
//
// All operations require a current GL context. Non-copyable (owns a GL name); movable.
class ALGPUBuffer
{
public:
    enum EFlags : U32
    {
        DYNAMIC  = 1 << 0,  // upload() may rewrite it (GL_DYNAMIC_STORAGE_BIT)
        READBACK = 1 << 1,  // the CPU reads it: mapped for reading for as long as it lives, where persistent maps
                            // resolved (GL 4.4), and kept in memory the CPU reads quickly
        STREAM   = 1 << 2,  // the CPU writes it often: mapped for writing for as long as it lives, where persistent
                            // maps resolved, and upload() copies into the map; elsewhere upload() is DYNAMIC's
    };

    ALGPUBuffer() = default;
    ~ALGPUBuffer();

    ALGPUBuffer(const ALGPUBuffer&) = delete;
    ALGPUBuffer& operator=(const ALGPUBuffer&) = delete;

    // Moving transfers the GL name and the mapping; the source is left empty.
    ALGPUBuffer(ALGPUBuffer&& other) noexcept;
    ALGPUBuffer& operator=(ALGPUBuffer&& other) noexcept;

    // Replaces any store this held. `data` may be null (contents undefined until written or cleared).
    // `label` names it for debug output (glObjectLabel when gGLManager.mHasDebugOutput). False, and empty, if GL could
    // not make the store.
    bool allocate(size_t bytes, U32 flags = 0, const void* data = nullptr, const char* label = nullptr);

    // Delete the GL object, if any. Call from an explicit teardown that still has a live GL context rather than
    // relying on destruction order at shutdown; the destructor calls it as a backstop, and issues no GL once the
    // context is gone.
    void release();

    bool   isAllocated() const { return mName != 0; }
    size_t size() const { return mSize; }
    U32    getGLName() const { return mName; }
    U32    getFlags() const { return mFlags; }

    // The persistent map of a READBACK or STREAM store, coherent both ways; null where persistent maps did not resolve
    // and for any other store. The CPU sees what the GPU wrote once a fence after the write has signalled, and the
    // GPU sees what the CPU wrote in any command issued after the write.
    U8* getMapped() const { return mMapped; }

    // Write `bytes` from `data` at `offset`. DYNAMIC and STREAM buffers only (asserted). A mapped STREAM store takes
    // it as a copy into the map, which the GPU must not be reading.
    void upload(size_t offset, const void* data, size_t bytes);

    // Set every 4-byte word to `value`.
    void clear(U32 value = 0);

    // Set every 4-byte word of [offset, offset + bytes) to `value`; both multiples of 4. GPU-side
    // (glClearBufferSubData) where it resolved; elsewhere an upload of the pattern, which needs DYNAMIC or STREAM
    // (asserted).
    void clearRange(size_t offset, size_t bytes, U32 value = 0);

    // Copy `bytes` of `src` from `src_offset` to `dst_offset` here, GPU-side. `src` may be this buffer, if the two
    // ranges do not overlap.
    void copyFrom(const ALGPUBuffer& src, size_t src_offset, size_t dst_offset, size_t bytes);

    // Read `bytes` at `offset` into `out`. Synchronous: waits for the GPU. Tests and debugging only; ALGPUReadback
    // reads back without waiting.
    bool read(size_t offset, void* out, size_t bytes) const;

    // Every live ALGPUBuffer's store, in bytes.
    static U64 sBytesAllocated;

private:
    U32    mName   = 0;  // GL buffer name (0 == unallocated)
    size_t mSize   = 0;
    U32    mFlags  = 0;
    U8*    mMapped = nullptr;
};

// Results the GPU wrote, read back without waiting on it. request() copies a range of a buffer into the next of a few
// slots, GPU-side, and fences it; collect() hands back the oldest result once its fence has signalled, which is a frame
// or two later. A request that finds every slot holding a result not yet collected is refused rather than stalling on
// the oldest. Slots are mapped for reading where persistent maps resolved, and read by glGetBufferSubData elsewhere --
// after the fence, so neither waits.
//
// All operations require a current GL context. Non-copyable.
class ALGPUReadback
{
public:
    ALGPUReadback() = default;
    ~ALGPUReadback();

    ALGPUReadback(const ALGPUReadback&) = delete;
    ALGPUReadback& operator=(const ALGPUReadback&) = delete;

    // Replaces what this held: `slots` results of up to `bytes` each may be in flight at once.
    bool allocate(size_t bytes, U32 slots = 3, const char* label = nullptr);
    void release();

    bool   isAllocated() const { return mStore.isAllocated(); }
    size_t getSlotSize() const { return mSlotBytes; }
    // Requests made and not yet collected.
    U32    getPending() const { return mPending; }

    // Copy [src_offset, src_offset + bytes) of `src` into the next slot and fence the copy. A shader's writes to `src`
    // are ordered before the copy here. False, and nothing copied, while every slot is pending.
    bool request(const ALGPUBuffer& src, size_t src_offset, size_t bytes);

    // The oldest pending result into `out`, the `bytes` its request copied, once the GPU has finished it: false while
    // it is still in flight, or when nothing is pending. `wait` waits for it instead, for a teardown or a test.
    bool collect(void* out, size_t bytes, bool wait = false);

private:
    struct Slot
    {
        void*  mFence   = nullptr;  // the GLsync after the slot's copy
        size_t mBytes   = 0;
        bool   mFlushed = false;    // the fence has been flushed toward the GPU
    };

    ALGPUBuffer       mStore;
    std::vector<Slot> mSlots;
    size_t            mSlotBytes  = 0;  // what a slot holds
    size_t            mSlotStride = 0;  // the distance between slots in mStore
    U32               mNext       = 0;  // the slot the next request fills
    U32               mPending    = 0;  // the oldest pending slot is mPending behind mNext
};

// Data the CPU writes for the GPU every frame, without waiting on an upload or on the GPU: a ring of regions in one
// STREAM buffer. map() gives the next region to write, publish() hands it to the GPU, and the GPU has it until the ring
// comes round to it again, `regions` publishes later; only a GPU that many frames behind makes map() wait. Where
// persistent maps did not resolve, map() gives memory of its own and publish() uploads it.
//
// A published region's reads -- binds, draws, dispatches -- are all issued before the next map(), which fences them.
//
// All operations require a current GL context. Non-copyable.
class ALGPUStream
{
public:
    ALGPUStream() = default;
    ~ALGPUStream();

    ALGPUStream(const ALGPUStream&) = delete;
    ALGPUStream& operator=(const ALGPUStream&) = delete;

    // Replaces what this held: `regions` regions of up to `bytes` each.
    bool allocate(size_t bytes, U32 regions = 3, const char* label = nullptr);
    void release();

    bool   isAllocated() const { return mStore.isAllocated(); }
    size_t getRegionSize() const { return mRegionBytes; }
    // The buffer a published region lies in, for LLRender::bindStorageBuffer and the indirect calls.
    const ALGPUBuffer& getBuffer() const { return mStore; }

    // Where the next region's bytes go, getRegionSize() of them. Fences the region published last.
    U8* map();

    // Hand the GPU the `bytes` written since map(). Returns where they start in getBuffer(): a multiple of
    // gGLManager.mShaderStorageBufferOffsetAlignment, so a storage range can start there.
    size_t publish(size_t bytes);

    // map() calls that found their region still in use by the GPU, and waited.
    U32 getStalls() const { return mStalls; }

private:
    ALGPUBuffer        mStore;
    std::vector<void*> mFences;            // per region, the GLsync after the last reads of it
    std::vector<U8>    mStaging;           // what map() gives where mStore is not mapped
    size_t             mRegionBytes  = 0;
    size_t             mRegionStride = 0;
    S32                mPublished    = -1; // the region published last, not yet fenced
    S32                mWriting      = -1; // the region map() gave, not yet published
    U32                mNext         = 0;  // the region the next map() gives
    U32                mStalls       = 0;
};
