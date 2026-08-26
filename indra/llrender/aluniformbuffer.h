/**
 * @file aluniformbuffer.h
 * @brief RAII wrapper around a GL uniform buffer object (UBO).
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

#include <vector>

// RAII wrapper around a GL uniform buffer object (UBO). Encapsulates the
// glGenBuffers / glBindBuffer / glBufferData / glBindBufferBase / glDeleteBuffers lifecycle so
// the engine's shared constant blocks (reflection probes, environment, ...) don't issue bare
// GL from newview. update() replaces the whole store by orphaning (glBufferData), which suits
// per-frame streamed constant blocks; bind() attaches it to an indexed uniform-buffer point
// (the fixed engine points in LLGLSLShader::UniformBlock).
//
// For HIGH-FREQUENCY blocks rewritten between draws (the matrix stack), the shadowed
// API (initShadowed/beginWrite/endWrite/flush/bindCurrent) keeps a client copy and uploads per
// sUpdateMode. glBufferSubData into an in-flight UBO forces an implicit sync on some drivers
// (Apple's GL-on-Metal most of all), so several streaming strategies (EUpdateMode) are provided
// for comparison, from a per-draw orphan through a persistently-mapped ring.
//
// All uploads/binds are DSA (glNamed*) when GL 4.5 is present, dropping the bind-target bracket.
//
// All operations require a current GL context. Non-copyable (owns a GL name).
class ALUniformBuffer
{
public:
    // GL usage hint for update(). STREAM (the default) == rewritten roughly once per use, the
    // right choice for per-frame constant blocks.
    enum class EUsage { STREAM, DYNAMIC, STATIC };

    // Upload strategy for shadowed buffers (see flush()). Mirrors the
    // AlchemyRenderUBOUpdateMode debug setting; switchable at runtime.
    enum EUpdateMode : S32
    {
        UPDATE_DIRECT       = 0, // glBufferSubData at each write's range (baseline)
        UPDATE_ORPHAN       = 1, // accumulate in the shadow; orphan + re-upload whole store per draw
        UPDATE_RING         = 2, // accumulate in the shadow; per draw, copy into a shared ring
                                 // (unsynchronized map/unmap) and glBindBufferRange the slice
        UPDATE_PERSISTENT   = 3, // accumulate in the shadow; per draw, memcpy into a persistently
                                 // mapped COHERENT ring (ARB_buffer_storage, no per-draw map/unmap
                                 // or orphan) and glBindBufferRange the slice. Segment fences keep
                                 // the CPU from overwriting a region the GPU may still be reading.
                                 // Falls back to UPDATE_ORPHAN when buffer-storage is unavailable.
        UPDATE_PERSISTENT_FLUSH = 4, // as UPDATE_PERSISTENT but the ring is mapped NON-coherent
                                 // (GL_MAP_FLUSH_EXPLICIT_BIT); each slice is published with
                                 // glFlushMappedNamedBufferRange. Faster than coherent on drivers
                                 // that place coherent mappings in uncached/write-combined memory.
        // NOTE: a mode 5 existed here (UPDATE_BINDLESS_NV -- every UBO bind issued as an
        // NV bindless GPU address via glBufferAddressRangeNV under
        // GL_UNIFORM_BUFFER_UNIFIED_NV). It was removed: address sourcing gives no orphan
        // protection, so update()'s glBufferData respecify could free a store out from
        // under in-flight draws, and keeping addresses fresh forced a
        // resident/non-resident cycle per buffer per frame -- which cost more than the
        // per-bind validation the mode existed to skip. Do not reintroduce it without
        // solving both.
    };
    static S32 sUpdateMode;

    // Clamp a settings value to a live mode. Guards against a stale persisted 5 (the
    // removed NV bindless mode) or any out-of-range value silently degrading to the
    // switch default instead of the mode the user believes is active.
    static S32 clampUpdateMode(S32 mode);

    ALUniformBuffer() = default;
    ~ALUniformBuffer();

    ALUniformBuffer(const ALUniformBuffer&) = delete;
    ALUniformBuffer& operator=(const ALUniformBuffer&) = delete;

    // Movable so it can live by value in movable owners (e.g. GLTF Skin/Asset held in
    // std::vector). Moving transfers ownership of the GL name; the source is left empty.
    ALUniformBuffer(ALUniformBuffer&& other) noexcept;
    ALUniformBuffer& operator=(ALUniformBuffer&& other) noexcept;

    // Replace the buffer's entire contents with `bytes` bytes from `data`, allocating the GL
    // object on first use.
    void update(const void* data, size_t bytes, EUsage usage = EUsage::STREAM);

    // Bind the whole buffer to indexed uniform-buffer binding point `binding`
    // (glBindBufferBase). No-op if the buffer has never been given data.
    void bind(U32 binding) const;

    // Bind the sub-range [offset, offset + size) to indexed uniform-buffer binding point
    // `binding` (glBindBufferRange) -- e.g. addressing one material's slice of a packed
    // constant array. No-op if the buffer has never been given data.
    void bindRange(U32 binding, size_t offset, size_t size) const;

    // --- Shadowed streaming API ------------------------------------------------

    // Allocate a zero-filled client shadow AND a zero-filled GL store of `bytes`
    // bytes. Members never written stay a well-defined zero in every mode.
    void initShadowed(size_t bytes);

    // Write into the shadow, then endWrite() the touched range. In UPDATE_DIRECT
    // endWrite uploads the range immediately (glBufferSubData); otherwise it just
    // marks the buffer dirty for the next flush()/bindCurrent().
    U8*  beginWrite() { return mShadow.data(); }
    void endWrite(size_t offset, size_t bytes);

    // Upload pending shadow contents per sUpdateMode and (re)attach the buffer at
    // `binding`. Called before every draw for dirty buffers, so it early-outs
    // cheaply when clean; UPDATE_RING additionally rebinds even when clean only
    // via bindCurrent().
    void flush(U32 binding);

    // Re-attach at `binding` on a program switch: base-bind for DIRECT/ORPHAN;
    // for RING, rebind the last slice, re-flushing if the ring wrapped past it.
    void bindCurrent(U32 binding);

    // Per-draw guard for a block that lives at a DEDICATED engine binding point the
    // caller has already attached once (bindCurrent) and nothing else ever claims
    // (UB_MATRICES). Uploads via flush() when dirty; otherwise re-flushes ONLY when
    // streaming-ring reuse has invalidated the last slice's bytes (mode switch, RING
    // orphan-on-wrap, persistent segment departure -- bindCurrent's liveness rules).
    // Unlike bindCurrent it never re-issues a redundant bind, so the clean-and-live
    // path -- the overwhelmingly common one -- makes no GL calls at all.
    void ensureCurrent(U32 binding);

    // Monotonic counter, bumped each time the shared UPDATE_RING ring is orphaned on wrap
    // -- the one event that invalidates EVERY outstanding slice at once (see flushRing).
    // A caller that validates several ring-backed blocks in sequence before a draw reads this
    // before and after: an advance means a later flush wrapped and stranded the slices bound
    // before it, which must be re-validated. Constant outside UPDATE_RING -- ORPHAN/DIRECT keep
    // own stores and the persistent ring never orphans mid-sequence -- so a guard on it runs
    // exactly once there.
    static U32 ringWrapEpoch();

    bool dirty() const { return mDirty; }

    // Delete the GL object (if any). Call from an explicit teardown that still has a live GL
    // context rather than relying on destruction order at shutdown. The destructor also calls
    // this as a backstop.
    void release();

    // Delete the shared streaming ring (UPDATE_RING). Safe without a live context.
    static void cleanupClass();

    // --- Ring pressure telemetry ---------------------------------------------
    // Ring consumption is driven by the NUMBER of slices, not their size: every flush
    // takes a GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT-rounded slot (256 bytes on most
    // drivers), so a 64-byte block and a 2 KB block cost the same. Once a frame's slice
    // traffic laps the whole ring, the segment fence in flushPersistent() stops being
    // free and starts blocking on the GPU -- and it does so QUIETLY (one warning, then
    // it proceeds), which is exactly the shape of an unexplained frame-time cliff.
    // These counters make the pressure visible before it becomes one.
    struct RingStats
    {
        U32 mSlices        = 0; // flushes that consumed a slot
        U64 mBytes         = 0; // aligned bytes consumed
        U32 mSegmentSteps  = 0; // segment advances (persistent ring only)
        U32 mWraps         = 0; // full laps -- outstanding slices invalidated
        U32 mStalls        = 0; // segment fences that actually blocked on the GPU
        U32 mStallTimeouts = 0; // ... and gave up after the 1s timeout
        U32 mSegmentMask   = 0; // bit per segment LEFT by an advance this frame
        U32 mChurnFlushes  = 0; // re-flushes of UNCHANGED blocks that lost slice liveness
        U32 mLiveRebinds   = 0; // rebinds that reused a live slice (the cheap path)
    };

    // Close the current frame's telemetry: log it, warn if the ring lapped, reset the
    // accumulators, and resize each ring to the traffic it has been carrying. Call once per
    // rendered frame, at the END of it -- past the buffer swap, as display() does. A resize
    // deletes and remaps a ring and invalidates every outstanding slice, so calling this from
    // inside a frame makes that frame's own draws pay for it.
    static void endFrame();

    // Last completed frame's counters, for a stats readout. Persistent ring == modes
    // 3/4; the UPDATE_RING (mode 2) ring and the scratch ring are counted separately.
    // (In the scratch stats, mLiveRebinds counts scratchBindRange calls DEDUPED against
    // the already-live range, and mSlices counts payload writes.)
    static const RingStats& lastFramePersistentStats();
    static const RingStats& lastFrameRingStats();
    static const RingStats& lastFrameScratchStats();

    // --- Scratch ring: per-draw ranged engine-block payloads ------------------------
    // (rigged-mesh skinning palettes at UB_OBJECT_SKIN). A caller writes a payload
    // once per frame (scratchWrite -> ring offset) and re-binds its range cheaply on
    // every subsequent use (scratchBindRange) -- upload-once, bind-per-use. Backed by a
    // persistently-mapped fenced ring when buffer-storage is available (a write is a
    // plain memcpy; the map/unmap-per-write fallback measurably stalled for milliseconds
    // in late-frame passes), and by its own buffer either way so the shadowed streaming
    // machinery never interacts. scratchGeneration() bumps when the ring laps (or the
    // fallback orphans, GL restarts, or the ring is resized): cached offsets from an older
    // generation are stale and must be re-written. The persistent ring is sized adaptively from
    // observed per-frame traffic (small floor, grown toward the cap in endFrame), so a light
    // scene costs little; payloads over the floor's segment (a generous multiple of any real
    // skinning palette) are rejected (-1). Main-thread only, like the rest of the class.
    static S64  scratchWrite(const void* data, size_t bytes); // ring offset, or -1 on failure
    static void scratchBindRange(U32 binding, S64 offset, size_t bytes);
    static U32  scratchGeneration();

    bool   allocated() const { return mName != 0; }
    size_t size() const { return mSize; }
    U32    getGLName() const { return mName; }

private:
    void flushRing(U32 binding);
    void flushPersistent(U32 binding);

    // The mode used this call: UPDATE_PERSISTENT / _FLUSH degrade to UPDATE_ORPHAN when the
    // persistent mapping can't be created, so the rest of the code only special-cases live modes.
    static S32 effectiveMode();

    U32    mName = 0;   // GL buffer name (0 == unallocated)
    size_t mSize = 0;   // current byte size of the store

    // Shadowed streaming state
    std::vector<U8> mShadow;
    bool   mDirty = false;
    size_t mRingOffset = 0;  // this buffer's latest slice in the shared ring
    U32    mRingGeneration = 0; // ring generation the slice was written in (0 = never)
    // GL name of the buffer holding this block's currently-valid GPU data (mName for
    // DIRECT/ORPHAN, sRing for RING, sPersistRing for the persistent modes). bindCurrent
    // re-flushes when the active mode's backing buffer differs from this, so a cached slice is
    // never rebound against the wrong ring after a mode switch or a ring recreation. The RING and
    // persistent rings keep independent generation counters, so mRingGeneration alone can't tell
    // them apart -- this can. (U32 not GLuint: this header is included where GL types aren't.)
    U32    mSliceRing = 0;
};
