/**
 * @file aluniformbuffer.cpp
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

#include "linden_common.h"

#include "aluniformbuffer.h"

#include "llgl.h"          // gGLManager (context-alive guard)
#include "llglheaders.h"

namespace
{
    GLenum to_gl_usage(ALUniformBuffer::EUsage usage)
    {
        switch (usage)
        {
        case ALUniformBuffer::EUsage::DYNAMIC: return GL_DYNAMIC_DRAW;
        case ALUniformBuffer::EUsage::STATIC:  return GL_STATIC_DRAW;
        case ALUniformBuffer::EUsage::STREAM:
        default:                               return GL_STREAM_DRAW;
        }
    }

    // Frames of sustained low traffic before an adaptively-sized ring shrinks (below) back toward
    // the observed traffic. Long enough (~4 s at 60 fps) that a scene which merely fluctuates -- a
    // crowd that comes and goes -- doesn't thrash the ring; growth is always immediate. Shared by
    // all three streaming rings.
    constexpr int RING_SHRINK_COOLDOWN = 240;

    // Shared streaming ring for UPDATE_RING: every dirty shadowed buffer appends its contents and
    // binds its slice, so no store is ever rewritten while the GPU may still be reading it. On wrap
    // the whole ring is orphaned (glBufferData respecify -- the driver keeps the old store alive for
    // in-flight draws) and the generation bumped, invalidating every outstanding slice (owners
    // re-flush on next bindCurrent). Sized adaptively from observed traffic (endFrame) like the
    // other rings: a small floor grown toward the cap (and shrunk back when traffic drops) keeps a
    // light scene's footprint -- and the orphan-on-wrap churn -- down. No fences here, so the cap is
    // smaller than the persistent ring's.
    constexpr size_t RING_MIN_BYTES    = 1 * 1024 * 1024;  // initial ring, and the floor
    constexpr size_t RING_MAX_BYTES    = 4 * 1024 * 1024;  // grow cap (the old fixed size)
    constexpr int    RING_SLACK_FRAMES = 4;                // keep >= this many frames before a wrap
    static_assert(RING_MIN_BYTES <= RING_MAX_BYTES, "ring floor must not exceed the cap");

    GLuint sRing           = 0;
    size_t sRingHead       = 0;
    U32    sRingGeneration = 1;
    GLint  sRingAlign      = 256; // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, queried on creation
    size_t sRingBytes      = RING_MIN_BYTES; // ring size; sized adaptively in endFrame
    int    sRingShrinkCd   = 0;              // frames of low traffic toward a shrink

    // Scratch ring for per-draw ranged engine-block payloads (see the header), written
    // through scratchWrite() by callers that cache their offsets across many binds
    // (upload-once, bind-per-use), so the generation bumps on GL teardown as well as on
    // wrap -- a cached offset must never survive into a recreated buffer.
    //
    // Preferred backing is a persistently-mapped COHERENT ring with fenced segments
    // (the same scheme as sPersistRing below): a write is then a plain memcpy. The
    // map/unmap-per-write idiom this replaced (GL_STREAM_DRAW + unsynchronized map,
    // whole-store orphan on wrap) was measured stalling for MILLISECONDS on a single
    // palette write inside LLDrawPoolAlpha::renderPostDeferred -- rigged alpha-only
    // skins take their first write of the frame there, the GPU is a frame deep by
    // then, and a driver that services the map or the orphan synchronously parks the
    // CPU right on it. That idiom survives only as the fallback when buffer-storage
    // is unavailable (macOS GL 4.1). Coherency is deliberately not user-tunable here
    // (unlike sPersistRing): each payload is written once and never revisited, which
    // write-combined memory is fine at, so a mode knob would only add a recreate path.
    // Sizing. The persistent ring is sized adaptively from observed per-frame traffic (endFrame),
    // between SCRATCH_MIN_BYTES and SCRATCH_MAX_BYTES: most scenes stream a few hundred KiB of
    // palettes per frame, far under the old fixed 16 MiB, so a light scene keeps a tiny footprint
    // while a crowded region grows to the slack it needs (and shrinks back when the crowd leaves). A payload must fit ONE segment, so the
    // accepted payload cap is the SMALLEST ring's segment (segments only ever grow); that cap is
    // ~25x a full 152-bone Bento palette, so real skinning never approaches it. Segment COUNT stays
    // fixed at 4 across sizes -- growth doubles segment SIZE, not count (see the sPersistRing note).
    constexpr size_t SCRATCH_MIN_BYTES    = 1 * 1024 * 1024;  // initial persistent ring, and the floor
    constexpr size_t SCRATCH_MAX_BYTES    = 16 * 1024 * 1024; // grow cap
    constexpr int    SCRATCH_SEGMENTS     = 4;
    constexpr size_t SCRATCH_MAX_PAYLOAD  = 256 * 1024;       // scratchWrite reject cap
    constexpr size_t SCRATCH_BYTES        = 4 * 1024 * 1024;  // fixed orphan fallback ring (macOS GL 4.1)
    constexpr int    SCRATCH_SLACK_FRAMES = 4;                // keep >= this many frames of head slack
    static_assert(SCRATCH_MAX_PAYLOAD <= SCRATCH_MIN_BYTES / SCRATCH_SEGMENTS, "payload must fit the smallest ring's segment");
    static_assert(SCRATCH_MAX_PAYLOAD <= SCRATCH_BYTES, "payload must fit the orphan fallback ring");
    static_assert(SCRATCH_MIN_BYTES <= SCRATCH_MAX_BYTES, "scratch ring floor must not exceed the cap");

    GLuint sScratch             = 0;
    U8*    sScratchPtr          = nullptr; // live persistent mapping (null == fallback path)
    size_t sScratchHead         = 0;
    U32    sScratchGeneration   = 1;
    GLint  sScratchAlign        = 256; // GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, queried on creation
    int    sScratchSeg          = 0;
    size_t sScratchPersistBytes = SCRATCH_MIN_BYTES; // persistent-ring size; sized adaptively in endFrame
    int    sScratchShrinkCd     = 0;                 // frames of low traffic toward a shrink
    GLsync sScratchFence[SCRATCH_SEGMENTS] = {};
    bool   sScratchPersistTried = false; // persistent creation attempted (no retry spam)

    // Last range issued by scratchBindRange, so repeat binds of the same slice are
    // skipped: depth-sorted alpha ping-pongs between the same few skins, and the
    // shader-keyed uploadMatrixPalette overload re-applies on every program switch
    // even though indexed UBO bindings are context state, not program state. Keyed on
    // the generation (wrap and teardown must re-issue) and cleared whenever another
    // buffer claims the same indexed point through bind()/bindRange().
    U32    sScratchBoundBinding = 0xFFFFFFFFu;
    S64    sScratchBoundOffset  = -1;
    size_t sScratchBoundBytes   = 0;
    U32    sScratchBoundGen     = 0;

    // --- UPDATE_PERSISTENT / _FLUSH: persistently-mapped ring ------------------
    // One immutable (glBufferStorage) buffer, mapped once for the process lifetime. Each dirty
    // block memcpys its slice straight into the mapping (no map/unmap, no orphan, no
    // glBufferSubData) and binds the slice. The ring is split into PERSIST_SEGMENTS; a GLsync
    // fenced when we leave a segment is waited on before that segment is reused a lap later, so
    // the CPU never overwrites bytes the GPU may still be reading. sPersistCoherent records
    // whether the live mapping is COHERENT (mode 3) or non-coherent + explicit-flush (mode 4);
    // switching between the two recreates the ring.
    //
    // Sized adaptively from measured traffic (endFrame), the same scheme as the scratch ring: a
    // small floor grown toward the cap (and shrunk back as traffic falls), so a light scene costs
    // little while a busy one gets the slack it needs. Consumption is COUNT-driven -- each slice
    // takes a GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT-rounded slot, so a busy scene streaming
    // ~3.4 MB/frame is ~6k slices regardless of block size. We keep >= PERSIST_SLACK_FRAMES of head slack, which also holds each segment near
    // one frame of slices so a clean block is left (and re-flushed) at most ~once per frame --
    // undersizing below that would multiply that churn. The old fixed size was 16 MiB (~4.8 frames
    // for a busy scene); the cap preserves that ceiling.
    //
    // Segment COUNT deliberately stays at 4, so a grow doubles segment SIZE rather than adding
    // segments. Bigger segments halve the advance rate -- and every advance invalidates outstanding
    // slices (see bindCurrent), forcing clean blocks to re-flush -- so fewer, bigger segments mean
    // less of that churn (the reason a smaller floor trades memory for a little more of it).
    constexpr size_t PERSIST_MIN_BYTES    = 1 * 1024 * 1024;  // initial ring, and the floor
    constexpr size_t PERSIST_MAX_BYTES    = 16 * 1024 * 1024; // grow cap
    constexpr int    PERSIST_SEGMENTS     = 4;
    constexpr int    PERSIST_SLACK_FRAMES = 4;                // keep >= this many frames of head slack
    static_assert(PERSIST_MIN_BYTES <= PERSIST_MAX_BYTES, "persistent ring floor must not exceed the cap");

    GLuint sPersistRing       = 0;
    U8*    sPersistPtr        = nullptr; // the live persistent mapping (== whole ring)
    size_t sPersistHead       = 0;
    U32    sPersistGeneration = 1;       // bumped on full wrap; invalidates outstanding slices
    GLint  sPersistAlign      = 256;
    int    sPersistSeg        = 0;
    size_t sPersistBytes      = PERSIST_MIN_BYTES; // ring size; sized adaptively in endFrame
    int    sPersistShrinkCd   = 0;                 // frames of low traffic toward a shrink
    GLsync sPersistFence[PERSIST_SEGMENTS] = {};
    bool   sPersistTried      = false;   // creation attempted (at sPersistTriedCoherent)
    bool   sPersistTriedCoherent = true; // coherency of that attempt: a switch to the OTHER
                                         // coherency retries rather than staying pinned to ORPHAN
    bool   sPersistOK         = false;   // mapping is live and usable
    bool   sPersistCoherent   = true;    // coherency the live ring was created with

    // Ring pressure accumulators (see ALUniformBuffer::RingStats). Split per ring so a
    // mode comparison isn't muddied by the other ring's leftovers. Main-thread only,
    // like the rest of the class, so plain globals are enough.
    ALUniformBuffer::RingStats sPersistStats;
    ALUniformBuffer::RingStats sRingStats;
    ALUniformBuffer::RingStats sScratchStats;      // mLiveRebinds counts DEDUPED (skipped) rebinds here
    ALUniformBuffer::RingStats sPersistLastFrame;
    ALUniformBuffer::RingStats sRingLastFrame;
    ALUniformBuffer::RingStats sScratchLastFrame;

    // DSA (GL 4.5 / ARB_direct_state_access) drops the bind/unbind bracket around every buffer
    // upload/map. Resolved once; false -> the classic bind-target path is used.
    bool dsa_ok()
    {
        static const bool ok = glNamedBufferData && glNamedBufferSubData && glNamedBufferStorage
                            && glMapNamedBufferRange && glUnmapNamedBuffer && glFlushMappedNamedBufferRange;
        return ok;
    }

    // (Re)specify a buffer's whole store, DSA-aware.
    void buffer_data(GLuint name, GLsizeiptr size, const void* data, GLenum usage)
    {
        if (dsa_ok())
        {
            glNamedBufferData(name, size, data, usage);
        }
        else
        {
            glBindBuffer(GL_UNIFORM_BUFFER, name);
            glBufferData(GL_UNIFORM_BUFFER, size, data, usage);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
    }

    // Sub-update a buffer range, DSA-aware.
    void buffer_subdata(GLuint name, GLintptr offset, GLsizeiptr size, const void* data)
    {
        if (dsa_ok())
        {
            glNamedBufferSubData(name, offset, size, data);
        }
        else
        {
            glBindBuffer(GL_UNIFORM_BUFFER, name);
            glBufferSubData(GL_UNIFORM_BUFFER, offset, size, data);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
    }

    // Publish a non-coherent persistent mapping range (mode 4), DSA-aware.
    void flush_mapped(GLuint name, GLintptr offset, GLsizeiptr size)
    {
        if (dsa_ok())
        {
            glFlushMappedNamedBufferRange(name, offset, size);
        }
        else
        {
            glBindBuffer(GL_UNIFORM_BUFFER, name);
            glFlushMappedBufferRange(GL_UNIFORM_BUFFER, offset, size);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
    }

    // Round `v` up to a multiple of `align` (a power of two, as GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT
    // always is).
    inline size_t align_up(size_t v, size_t align)
    {
        return (v + align - 1) & ~(align - 1);
    }

    // Allocate a fresh buffer name, DSA-aware.
    void create_buffer(GLuint& name)
    {
        if (dsa_ok())
        {
            glCreateBuffers(1, &name);
        }
        else
        {
            glGenBuffers(1, &name);
        }
    }

    // Unmap a buffer's persistent/mapped range, DSA-aware.
    void buffer_unmap(GLuint name)
    {
        if (dsa_ok())
        {
            glUnmapNamedBuffer(name);
        }
        else
        {
            glBindBuffer(GL_UNIFORM_BUFFER, name);
            glUnmapBuffer(GL_UNIFORM_BUFFER);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
    }

    // Write `src` into [offset, offset+size) via an UNSYNCHRONIZED map (skips the GPU sync on a
    // fresh, never-in-flight ring region), falling back to glBufferSubData if the map fails.
    // DSA-aware. Shared by the two orphan-on-wrap rings (sRing and the scratch fallback).
    void buffer_write_unsync(GLuint name, GLintptr offset, GLsizeiptr size, const void* src)
    {
        const GLbitfield map_flags = GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
        void* dst = nullptr;
        if (dsa_ok())
        {
            dst = glMapNamedBufferRange(name, offset, size, map_flags);
            if (dst)
            {
                memcpy(dst, src, (size_t)size);
                glUnmapNamedBuffer(name);
            }
        }
        else
        {
            glBindBuffer(GL_UNIFORM_BUFFER, name);
            dst = glMapBufferRange(GL_UNIFORM_BUFFER, offset, size, map_flags);
            if (dst)
            {
                memcpy(dst, src, (size_t)size);
                glUnmapBuffer(GL_UNIFORM_BUFFER);
            }
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
        if (!dst)
        {
            buffer_subdata(name, offset, size, src);
        }
    }

    // Create an immutable (glBufferStorage) buffer and persistently map its whole range. Returns
    // the mapping (null on failure), DSA-aware. Shared by the two persistent rings.
    U8* create_mapped_ring(GLuint& name, GLsizeiptr size, GLbitfield stor_flags, GLbitfield map_flags)
    {
        void* ptr = nullptr;
        if (dsa_ok())
        {
            glCreateBuffers(1, &name);
            glNamedBufferStorage(name, size, nullptr, stor_flags);
            ptr = glMapNamedBufferRange(name, 0, size, map_flags);
        }
        else
        {
            glGenBuffers(1, &name);
            glBindBuffer(GL_UNIFORM_BUFFER, name);
            glBufferStorage(GL_UNIFORM_BUFFER, size, nullptr, stor_flags);
            ptr = glMapBufferRange(GL_UNIFORM_BUFFER, 0, size, map_flags);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
        return static_cast<U8*>(ptr);
    }

    // Advance a fenced, segmented ring so a slice of `aligned` bytes fits within one segment.
    // When the slice would spill past the current segment's end: fence the segment being LEFT,
    // wrap (bumping *generation*, which invalidates outstanding slices) on the last segment, and
    // before reusing the segment being ENTERED, wait out any draws that read it a lap ago. All
    // ring state is passed by reference so the one mechanism drives both the persistent streaming
    // ring and the persistent scratch ring. On return `head`/`seg` address the slice's home.
    void advance_fenced_ring(size_t aligned, size_t seg_bytes, int seg_count,
                             size_t& head, int& seg, GLsync* fences,
                             U32& generation, ALUniformBuffer::RingStats& stats,
                             const char* ring_label)
    {
        const size_t seg_end = (size_t)(seg + 1) * seg_bytes;
        if (head + aligned <= seg_end)
        {
            return; // fits in the current segment; nothing to fence
        }

        if (fences[seg])
        {
            glDeleteSync(fences[seg]);
        }
        fences[seg] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

        ++stats.mSegmentSteps;
        stats.mSegmentMask |= (U32)(1u << seg); // bit per segment LEFT (see RingStats)

        int next = seg + 1;
        if (next >= seg_count)
        {
            next = 0;
            ++generation; // full wrap: outstanding slices (mRingGeneration) go stale
            ++stats.mWraps;
        }
        seg  = next;
        head = (size_t)next * seg_bytes;

        if (fences[next])
        {
            // Poll before blocking so a real stall is distinguishable from the healthy path: a
            // signaled fence costs nothing; one that would BLOCK means this frame's slice traffic
            // has lapped the ring and the CPU is now waiting on the GPU inside the frame.
            if (glClientWaitSync(fences[next], 0, 0) == GL_TIMEOUT_EXPIRED)
            {
                LL_PROFILE_ZONE_NAMED_CATEGORY_DISPLAY("UBO ring fence stall");
                ++stats.mStalls;

                GLenum r = glClientWaitSync(fences[next], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull /* 1s */);
                if (r == GL_TIMEOUT_EXPIRED)
                {
                    ++stats.mStallTimeouts;
                    LL_WARNS_ONCE("ShaderUniform") << ring_label << " UBO ring: segment fence timed out; "
                                                      "ring may be undersized." << LL_ENDL;
                }
            }
            glDeleteSync(fences[next]);
            fences[next] = 0;
        }
    }

    // Destroy the persistent ring (fences, mapping, buffer). Bumps the generation so any
    // outstanding slice re-flushes on its next bindCurrent.
    void teardown_persist_ring()
    {
        if (sPersistRing != 0)
        {
            if (gGLManager.mInited)
            {
                for (auto& f : sPersistFence)
                {
                    if (f) { glDeleteSync(f); }
                }
                buffer_unmap(sPersistRing);
                glDeleteBuffers(1, &sPersistRing);
            }
            sPersistRing = 0;
        }
        // Clear the fence handles unconditionally: on a context-lost teardown the syncs are already
        // gone and cannot be deleted, but their handles must not survive into a recreated ring --
        // a later flushPersistent() would glDeleteSync/glClientWaitSync a dangling handle. (Matches
        // the scratch teardown in cleanupClass(), which zeroes its fences outside the mInited gate.)
        for (auto& f : sPersistFence)
        {
            f = 0;
        }
        sPersistPtr       = nullptr;
        sPersistHead      = 0;
        sPersistSeg       = 0;
        sPersistOK        = false;
        sPersistTried     = false;
        ++sPersistGeneration;
    }

    // Resize the persistent ring to `bytes` by tearing it down; the next flush recreates it (at the
    // active mode's coherency) via ensure_persist_ring. Safe at a frame boundary: in-flight draws
    // keep the old store alive via GL's deferred delete, and teardown's generation bump makes every
    // outstanding slice re-flush. Leaves the coherency to the recreate -- only the size changes.
    void resize_persist_ring(size_t bytes)
    {
        sPersistBytes = bytes;
        teardown_persist_ring();
    }

    // Free the UPDATE_RING ring; the next flushRing recreates it at sRingBytes. Bumps the generation
    // so outstanding slices re-flush, and (like the wrap orphan) in-flight draws keep the old store
    // alive via GL's deferred delete.
    void teardown_ring()
    {
        if (sRing != 0)
        {
            if (gGLManager.mInited)
            {
                glDeleteBuffers(1, &sRing);
            }
            sRing = 0;
        }
        sRingHead = 0;
        ++sRingGeneration;
    }

    // Resize the UPDATE_RING ring to `bytes` by tearing it down; the next flushRing recreates it.
    void resize_ring(size_t bytes)
    {
        sRingBytes = bytes;
        teardown_ring();
    }

    // Decide the next size for an adaptively-sized ring from this frame's aligned byte traffic.
    // `target` is the smallest power of two that keeps >= `slack` frames of head slack, clamped to
    // [floor, cap]. Grow to it AT ONCE; shrink to it only after the ring has sat oversized (a
    // power-of-two step or more above target) for RING_SHRINK_COOLDOWN frames -- so leaving a crowd
    // reclaims memory while a scene that merely fluctuates never thrashes. Returns `current` when
    // nothing should change; the caller resizes iff the result differs.
    size_t next_ring_size(size_t current, U64 frame_bytes, size_t floor_bytes, size_t cap_bytes,
                          int slack, int& shrink_cd)
    {
        const size_t want = (size_t)(frame_bytes * (U64)slack);

        size_t target = floor_bytes;
        while (target < want && target < cap_bytes)
        {
            target <<= 1;
        }
        target = llmin(target, cap_bytes);

        if (target > current)
        {
            shrink_cd = 0;
            return target; // grow immediately
        }
        if (target < current)
        {
            // Oversized (pow2 spacing => current is >= 2x target). Shrink only after a cooldown.
            if (++shrink_cd >= RING_SHRINK_COOLDOWN)
            {
                shrink_cd = 0;
                return target;
            }
            return current;
        }
        shrink_cd = 0; // right-sized
        return current;
    }

    // Create + map the persistent ring with the requested coherency on first use (or recreate it
    // when the coherency changes). Returns false (cached) when buffer-storage / persistent
    // mapping is unavailable, so callers fall back to ORPHAN.
    bool ensure_persist_ring(bool coherent)
    {
        if (sPersistOK)
        {
            if (sPersistCoherent == coherent)
            {
                return true;
            }
            teardown_persist_ring(); // coherency changed: rebuild
        }
        else if (sPersistTried && sPersistTriedCoherent == coherent)
        {
            // Already failed at THIS coherency; don't retry-spam. A request for the other
            // coherency still falls through and retries (a transient failure must not pin both
            // persistent modes to ORPHAN until cleanupClass).
            return false;
        }
        sPersistTried = true;
        sPersistTriedCoherent = coherent;

        // Entry points must be resolved (core 4.4 / ARB_buffer_storage + ARB_sync).
        if (!glBufferStorage || !glMapBufferRange || !glFenceSync || !glClientWaitSync || !glDeleteSync)
        {
            LL_WARNS("ShaderUniform") << "Persistent UBO: buffer-storage/sync entry points "
                                         "unavailable; falling back to ORPHAN." << LL_ENDL;
            return false;
        }

        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &sPersistAlign);
        sPersistAlign = llmax(sPersistAlign, 1);

        const GLbitfield stor_flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | (coherent ? GL_MAP_COHERENT_BIT : 0);
        const GLbitfield map_flags  = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT |
                                      (coherent ? GL_MAP_COHERENT_BIT : GL_MAP_FLUSH_EXPLICIT_BIT);
        U8* ptr = create_mapped_ring(sPersistRing, (GLsizeiptr)sPersistBytes, stor_flags, map_flags);

        if (!ptr)
        {
            LL_WARNS("ShaderUniform") << "Persistent UBO: glBufferStorage/glMapBufferRange failed; "
                                         "falling back to ORPHAN." << LL_ENDL;
            glDeleteBuffers(1, &sPersistRing);
            sPersistRing = 0;
            return false;
        }

        sPersistPtr      = ptr;
        sPersistHead     = 0;
        sPersistSeg      = 0;
        sPersistCoherent = coherent;
        sPersistOK       = true;
        return true;
    }

    // Create the scratch ring on first use: persistently-mapped + COHERENT when
    // buffer-storage is available, else the legacy orphan-on-wrap STREAM_DRAW ring
    // (see the scratch state block above for why). Returns false only when buffer
    // creation itself fails.
    bool ensure_scratch_ring()
    {
        if (sScratch != 0)
        {
            return true;
        }

        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &sScratchAlign);
        sScratchAlign = llmax(sScratchAlign, 1);

        if (!sScratchPersistTried)
        {
            sScratchPersistTried = true;
            if (glBufferStorage && glMapBufferRange && glFenceSync && glClientWaitSync && glDeleteSync)
            {
                // Storage and map flags are identical: WRITE + PERSISTENT + COHERENT.
                const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
                U8* ptr = create_mapped_ring(sScratch, (GLsizeiptr)sScratchPersistBytes, flags, flags);
                if (ptr)
                {
                    sScratchPtr  = ptr;
                    sScratchHead = 0;
                    sScratchSeg  = 0;
                    return true;
                }
                LL_WARNS("ShaderUniform") << "Scratch UBO ring: persistent mapping failed; "
                                             "falling back to the orphan ring." << LL_ENDL;
                if (sScratch != 0)
                {
                    glDeleteBuffers(1, &sScratch);
                    sScratch = 0;
                }
            }
        }

        create_buffer(sScratch);
        if (sScratch == 0)
        {
            return false;
        }
        buffer_data(sScratch, (GLsizeiptr)SCRATCH_BYTES, nullptr, GL_STREAM_DRAW);
        sScratchHead = 0;
        return true;
    }

    // Free the scratch ring (fences, mapping, buffer) and bump the generation so every cached
    // offset re-writes into whatever ring comes next. Shared by cleanupClass() and the adaptive
    // grow path. Leaves sScratchPersistBytes alone -- the caller owns the target size.
    void teardown_scratch_ring()
    {
        if (sScratch != 0)
        {
            if (gGLManager.mInited)
            {
                for (auto& f : sScratchFence)
                {
                    if (f) { glDeleteSync(f); }
                }
                if (sScratchPtr)
                {
                    buffer_unmap(sScratch);
                }
                glDeleteBuffers(1, &sScratch);
            }
            sScratch = 0;
        }
        // Zero the fence handles unconditionally: a context-lost teardown can't delete the syncs,
        // but their handles must not survive into a recreated ring.
        for (auto& f : sScratchFence)
        {
            f = 0;
        }
        sScratchPtr          = nullptr;
        sScratchHead         = 0;
        sScratchSeg          = 0;
        sScratchPersistTried = false;        // let the recreated ring attempt the persistent mapping again
        ++sScratchGeneration;                // cached offsets must not survive into the new ring
        sScratchBoundBinding = 0xFFFFFFFFu;  // nor may the bind dedup memo
        sScratchBoundOffset  = -1;
    }

    // Resize the persistent scratch ring to `bytes` by tearing it down; the next scratchWrite
    // recreates it at the new size. Safe at a frame boundary: in-flight draws keep the old store
    // alive via GL's deferred delete, and teardown's generation bump makes every owner re-write.
    void resize_scratch_ring(size_t bytes)
    {
        sScratchPersistBytes = bytes;
        teardown_scratch_ring();
    }
}

S32 ALUniformBuffer::sUpdateMode = ALUniformBuffer::UPDATE_ORPHAN;

// static
S32 ALUniformBuffer::clampUpdateMode(S32 mode)
{
    if (mode < UPDATE_DIRECT || mode > UPDATE_PERSISTENT_FLUSH)
    {
        LL_WARNS_ONCE("ShaderUniform") << "UBO update mode " << mode
                                       << " is not a live strategy (mode 5, NV bindless, was removed); using "
                                       << UPDATE_ORPHAN << "." << LL_ENDL;
        return UPDATE_ORPHAN;
    }
    return mode;
}

// The mode actually used this call. UPDATE_PERSISTENT / _FLUSH degrade to UPDATE_ORPHAN when the
// persistent mapping can't be created. The rest of the code then only special-cases known-live
// modes.
S32 ALUniformBuffer::effectiveMode()
{
    switch (sUpdateMode)
    {
        case UPDATE_PERSISTENT:
            return ensure_persist_ring(true)  ? UPDATE_PERSISTENT       : UPDATE_ORPHAN;
        case UPDATE_PERSISTENT_FLUSH:
            return ensure_persist_ring(false) ? UPDATE_PERSISTENT_FLUSH : UPDATE_ORPHAN;
        default:
            return sUpdateMode;
    }
}

ALUniformBuffer::~ALUniformBuffer()
{
    release();
}

ALUniformBuffer::ALUniformBuffer(ALUniformBuffer&& other) noexcept
    : mName(other.mName)
    , mSize(other.mSize)
    , mShadow(std::move(other.mShadow))
    , mDirty(other.mDirty)
    , mRingOffset(other.mRingOffset)
    , mRingGeneration(other.mRingGeneration)
    , mSliceRing(other.mSliceRing)
{
    other.mName = 0;
    other.mSize = 0;
    other.mDirty = false;
    other.mRingOffset = 0;
    other.mRingGeneration = 0;
    other.mSliceRing = 0;
}

ALUniformBuffer& ALUniformBuffer::operator=(ALUniformBuffer&& other) noexcept
{
    if (this != &other)
    {
        release();
        mName = other.mName;
        mSize = other.mSize;
        mShadow = std::move(other.mShadow);
        mDirty = other.mDirty;
        mRingOffset = other.mRingOffset;
        mRingGeneration = other.mRingGeneration;
        mSliceRing = other.mSliceRing;
        other.mName = 0;
        other.mSize = 0;
        other.mDirty = false;
        other.mRingOffset = 0;
        other.mRingGeneration = 0;
        other.mSliceRing = 0;
    }
    return *this;
}

void ALUniformBuffer::update(const void* data, size_t bytes, EUsage usage)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

    if (mName == 0)
    {
        create_buffer(mName);
    }

    // Orphan-and-replace: the whole store is rewritten, so hand the driver a fresh allocation
    // (glBufferData) rather than sub-updating, letting it avoid a sync with in-flight draws.
    buffer_data(mName, (GLsizeiptr)bytes, data, to_gl_usage(usage));

    mSize = bytes;
    mSliceRing = mName; // valid data now lives in the own store
}

// Another buffer claiming the scratch ring's binding point invalidates the
// scratchBindRange dedup memo, or a later identical-looking scratch bind would be
// skipped against the wrong buffer. Only bind()/bindRange() need the hook: the
// shadowed streaming paths (flush*/bindCurrent) attach at the fixed engine-block
// points, never at the scratch client's.
static void scratch_note_foreign_bind(U32 binding, GLuint name)
{
    if (binding == sScratchBoundBinding && name != sScratch)
    {
        sScratchBoundBinding = 0xFFFFFFFFu;
        sScratchBoundOffset  = -1;
    }
}

void ALUniformBuffer::bind(U32 binding) const
{
    if (mName == 0)
    {
        return;
    }
    scratch_note_foreign_bind(binding, mName);
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, mName);
}

void ALUniformBuffer::bindRange(U32 binding, size_t offset, size_t size) const
{
    if (mName == 0)
    {
        return;
    }
    scratch_note_foreign_bind(binding, mName);
    glBindBufferRange(GL_UNIFORM_BUFFER, binding, mName, (GLintptr)offset, (GLsizeiptr)size);
}

void ALUniformBuffer::initShadowed(size_t bytes)
{
    mShadow.assign(bytes, 0);
    mDirty = false;
    mRingGeneration = 0;

    // Zero-filled GL store so members never written read 0 in DIRECT mode too
    // (streaming modes re-upload the whole zero-initialized shadow every flush).
    update(mShadow.data(), bytes, EUsage::DYNAMIC);
}

void ALUniformBuffer::endWrite(size_t offset, size_t bytes)
{
    llassert(offset + bytes <= mShadow.size());
    if (sUpdateMode == UPDATE_DIRECT && mSliceRing == mName)
    {
        // Sub-update is only coherent when mName already holds the full block AND the
        // binding point references it. mSliceRing == mName guarantees both: initShadowed()'s
        // update() seeded the store, and the last flush/bind targeted the own store. After a
        // LIVE switch away from a streaming ring (mSliceRing still names the ring buffer,
        // whose slice the binding point may still reference while this program stays bound),
        // fall through to the deferred path instead -- flush() re-uploads the whole shadow
        // and restores the mName attachment.
        buffer_subdata(mName, (GLintptr)offset, (GLsizeiptr)bytes, mShadow.data() + offset);
    }
    else
    {
        mDirty = true;
    }
}

void ALUniformBuffer::flush(U32 binding)
{
    if (!mDirty)
    {
        return;
    }
    mDirty = false;

    switch (effectiveMode())
    {
        case UPDATE_PERSISTENT:
        case UPDATE_PERSISTENT_FLUSH:
            flushPersistent(binding);
            break;
        case UPDATE_RING:
            flushRing(binding);
            break;
        case UPDATE_ORPHAN:
        default:
            // Whole-store replace: glBufferData hands the driver a fresh allocation instead of
            // syncing against in-flight reads of the old one. The buffer NAME is unchanged, so
            // existing glBindBufferBase attachments stay valid -- EXCEPT after a live switch
            // away from a streaming mode, where the binding point may still reference a ring
            // slice (mSliceRing != mName), which would make draws ignore this upload. Restore
            // the classic attachment in that case only.
            buffer_data(mName, (GLsizeiptr)mShadow.size(), mShadow.data(), GL_STREAM_DRAW);
            if (mSliceRing != mName)
            {
                glBindBufferBase(GL_UNIFORM_BUFFER, binding, mName);
                mSliceRing = mName;
            }
            break;
    }
}

void ALUniformBuffer::flushPersistent(U32 binding)
{
    const size_t len = mShadow.size();
    if (len == 0)
    {
        return;
    }
    const size_t seg_bytes = sPersistBytes / PERSIST_SEGMENTS; // tracks the adaptive ring size
    // A single block must fit inside one segment. This never happens for the engine's fixed
    // constant blocks, but degrade gracefully rather than memcpy past the mapped ring in a release
    // build (the assert compiles out): re-specify the own store (initShadowed always allocated it)
    // and bind it whole, exactly as UPDATE_ORPHAN would.
    if (len > seg_bytes)
    {
        llassert(false);
        LL_WARNS_ONCE("ShaderUniform") << "Persistent UBO block (" << len << " bytes) exceeds segment size "
                                       << seg_bytes << "; falling back to whole-store orphan bind." << LL_ENDL;
        buffer_data(mName, (GLsizeiptr)len, mShadow.data(), GL_STREAM_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, mName);
        mSliceRing = mName;
        return;
    }

    // Advance (fencing the segment left, waiting out the segment entered) so the slice fits within
    // one segment. On a wrap the generation bumps, invalidating outstanding slices (see bindCurrent).
    const size_t aligned = align_up(len, (size_t)sPersistAlign);
    advance_fenced_ring(aligned, seg_bytes, PERSIST_SEGMENTS,
                        sPersistHead, sPersistSeg, sPersistFence,
                        sPersistGeneration, sPersistStats, "Persistent");

    // Write the slice. Coherent mappings need no explicit publish; non-coherent (mode 4) does.
    memcpy(sPersistPtr + sPersistHead, mShadow.data(), len);
    if (!sPersistCoherent)
    {
        flush_mapped(sPersistRing, (GLintptr)sPersistHead, (GLsizeiptr)len);
    }

    // Bind the slice. The segment fences guarantee this region is not being read by any
    // in-flight draw.
    glBindBufferRange(GL_UNIFORM_BUFFER, binding, sPersistRing, (GLintptr)sPersistHead, (GLsizeiptr)len);

    mRingOffset     = sPersistHead;
    mRingGeneration = sPersistGeneration;
    mSliceRing      = sPersistRing;
    sPersistHead   += aligned;

    ++sPersistStats.mSlices;
    sPersistStats.mBytes += aligned;
}

void ALUniformBuffer::flushRing(U32 binding)
{
    const size_t len = mShadow.size();
    if (len == 0)
    {
        return; // nothing to stream (matches flushPersistent's empty guard)
    }

    if (sRing == 0)
    {
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &sRingAlign);
        sRingAlign = llmax(sRingAlign, 1);
        create_buffer(sRing);
        buffer_data(sRing, (GLsizeiptr)sRingBytes, nullptr, GL_STREAM_DRAW);
    }

    // A block bigger than the whole ring can't be sliced into it. Never happens for the engine's
    // constant blocks (all far below the ring floor and GL_MAX_UNIFORM_BLOCK_SIZE), but degrade to a
    // whole-store orphan bind rather than mapping/binding an out-of-range slice and silently
    // uploading nothing -- mirrors flushPersistent's oversized fallback.
    if (len > sRingBytes)
    {
        llassert(false);
        LL_WARNS_ONCE("ShaderUniform") << "UBO ring block (" << len << " bytes) exceeds ring size "
                                       << sRingBytes << "; falling back to whole-store orphan bind." << LL_ENDL;
        buffer_data(mName, (GLsizeiptr)len, mShadow.data(), GL_STREAM_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, binding, mName);
        mSliceRing = mName;
        return;
    }

    const size_t aligned = align_up(len, (size_t)sRingAlign);
    if (sRingHead + aligned > sRingBytes)
    {
        // Wrapped: orphan the whole ring and invalidate every outstanding slice
        // (owners re-flush on their next bindCurrent).
        buffer_data(sRing, (GLsizeiptr)sRingBytes, nullptr, GL_STREAM_DRAW);
        sRingHead = 0;
        ++sRingGeneration;
        ++sRingStats.mWraps;
    }

    // Fresh, never-in-flight region: unsynchronized map skips the GPU sync.
    buffer_write_unsync(sRing, (GLintptr)sRingHead, (GLsizeiptr)len, mShadow.data());

    glBindBufferRange(GL_UNIFORM_BUFFER, binding, sRing, (GLintptr)sRingHead, (GLsizeiptr)len);
    mRingOffset     = sRingHead;
    mRingGeneration = sRingGeneration;
    mSliceRing      = sRing;

    ++sRingStats.mSlices;
    sRingStats.mBytes += aligned;
    sRingHead += aligned;
}

void ALUniformBuffer::bindCurrent(U32 binding)
{
    const S32 mode = effectiveMode();
    const bool persistent = (mode == UPDATE_PERSISTENT || mode == UPDATE_PERSISTENT_FLUSH);
    const bool streaming  = (mode == UPDATE_RING || persistent);

    // The buffer holding this block's valid GPU data depends on the mode. If the active mode
    // reads from a DIFFERENT buffer than the last flush wrote to -- a mode switch, or a
    // coherency-driven ring recreation -- the cached slice is stale (or, worse, its offset
    // belongs to the other ring). Force a re-upload so we never bind old bytes or a foreign
    // offset. (mRingGeneration alone can't catch this: RING and the persistent ring keep
    // independent counters that routinely collide at small values.)
    const GLuint target = streaming ? (persistent ? sPersistRing : sRing) : mName;
    if (mSliceRing != target)
    {
        mDirty = true;
    }

    if (streaming && !mShadow.empty())
    {
        // Persistent ring: rebinding an existing slice is safe ONLY while it still lives in the
        // current, not-yet-left segment of the current generation. There its fence hasn't been
        // recorded yet (it is created when the segment is LEFT), so a rebind draw issued now is
        // captured by that fence, and the slice can't be overwritten until a future generation
        // laps back to it -- which the fence then gates. A slice in an already-left segment would
        // be read AFTER its fence, so a later lap could overwrite it while the draw is still
        // reading -> intermittent corruption; re-flush a fresh slice in that case. (Slices never
        // straddle segments -- flushPersistent advances before a spill -- so the start segment
        // identifies the whole slice. RING is exempt: it orphans on wrap, keeping old storage
        // alive for in-flight rebinds, with only the generation guard below.)
        if (persistent)
        {
            const bool slice_live =
                !mDirty
                && mSliceRing == sPersistRing
                && mRingGeneration == sPersistGeneration
                && (size_t)(mRingOffset / (sPersistBytes / PERSIST_SEGMENTS)) == (size_t)sPersistSeg;

            if (!slice_live)
            {
                // A CLEAN block that lost liveness is pure churn: its bytes are unchanged
                // and it re-uploads only because the head moved to another segment (or
                // lapped), which the rule above invalidates wholesale. Measured at ~2% of
                // slices, so the liveness rule is NOT worth replacing with per-slice
                // fences; kept as a standing health metric because a regression that
                // starts invalidating slices would show up here first.
                if (!mDirty)
                {
                    ++sPersistStats.mChurnFlushes;
                }
                mDirty = true;
                flush(binding);
            }
            else
            {
                ++sPersistStats.mLiveRebinds;
                glBindBufferRange(GL_UNIFORM_BUFFER, binding, sPersistRing, (GLintptr)mRingOffset, (GLsizeiptr)mShadow.size());
            }
            return;
        }

        if (mDirty || mRingGeneration != sRingGeneration || sRing == 0)
        {
            // Dirty, never flushed under the current generation, or the ring wrapped past our
            // slice: upload fresh from the shadow, which also binds.
            mDirty = true;
            flush(binding);
        }
        else
        {
            glBindBufferRange(GL_UNIFORM_BUFFER, binding, sRing, (GLintptr)mRingOffset, (GLsizeiptr)mShadow.size());
        }
        return;
    }
    // ORPHAN / DIRECT (or persistent fallback): bind() attaches the own store; flush()
    // re-uploads to mName if the switch above marked us dirty.
    bind(binding);
    flush(binding);
}

// static
U32 ALUniformBuffer::ringWrapEpoch()
{
    // Only the UPDATE_RING ring orphans its whole store mid-frame; sRingGeneration bumps there
    // and nowhere a per-draw sequence can trip over, so it is the exact wrap signal callers need.
    return sRingGeneration;
}

void ALUniformBuffer::ensureCurrent(U32 binding)
{
    if (mDirty)
    {
        flush(binding);
        return;
    }

    const S32 mode = effectiveMode();
    const bool persistent = (mode == UPDATE_PERSISTENT || mode == UPDATE_PERSISTENT_FLUSH);
    const bool streaming  = (mode == UPDATE_RING || persistent);
    if (!streaming || mShadow.empty())
    {
        // DIRECT/ORPHAN keep valid bytes in the own store; its base attachment at the
        // dedicated point persists (only flush()'s orphan path ever re-binds, and only
        // after a live mode switch).
        return;
    }

    // Same staleness rules as bindCurrent(), which see. mSliceRing == 0 (never flushed
    // in any mode -- possible only while the shadow is still all zeros) also lands in
    // the re-flush branch rather than aliasing a never-created ring's name.
    const GLuint target = persistent ? sPersistRing : sRing;
    bool live = (mSliceRing == target) && (mSliceRing != 0);
    if (live)
    {
        live = persistent
            ? (mRingGeneration == sPersistGeneration
               && (size_t)(mRingOffset / (sPersistBytes / PERSIST_SEGMENTS)) == (size_t)sPersistSeg)
            : (mRingGeneration == sRingGeneration);
    }
    if (!live)
    {
        // A clean block that lost slice liveness is the churn case bindCurrent() also
        // counts -- keep the health metric honest from this path too.
        if (persistent)
        {
            ++sPersistStats.mChurnFlushes;
        }
        mDirty = true;
        flush(binding);
    }
}

void ALUniformBuffer::release()
{
    if (mName != 0)
    {
        // Only issue GL if the context is still alive: the destructor can run during shutdown
        // after GL teardown (e.g. a singleton owner), where the name is already invalid and
        // the driver has reclaimed the storage. Mirrors LLVertexBuffer's gGLManager.mInited
        // gate.
        if (gGLManager.mInited)
        {
            glDeleteBuffers(1, &mName);
        }
        mName = 0;
        mSize = 0;
    }
    mShadow.clear();
    mShadow.shrink_to_fit();
    mDirty = false;
    mRingGeneration = 0;
    mSliceRing = 0;
}

// static
void ALUniformBuffer::endFrame()
{
    sPersistLastFrame = sPersistStats;
    sRingLastFrame    = sRingStats;
    sScratchLastFrame = sScratchStats;

    // A lone wrap is NOT pressure: the head walks continuously across frames, so it crosses the
    // ring-end boundary in SOME frame every few frames even when perfectly healthy (0 stalls, with
    // the frame's traffic far under the ring). Real pressure at the cap is a GPU stall (we blocked
    // on a segment fence) or a within-frame FULL LAP -- one frame's aligned traffic >= the whole
    // ring, so the head lapped itself and invalidated slices written earlier this same frame. Only
    // those mean the cap is too small; below the cap the adaptive sizing grows the ring anyway.
    //
    // Guarded by a plain static, NOT LL_WARNS_ONCE: that macro de-duplicates on the formatted
    // MESSAGE STRING (llerror.cpp, mUniqueLogMessages), so interpolating per-frame counters makes
    // every line unique -- it would print every frame AND leak one permanent map entry per frame.
    static bool s_warned_pressure = false;
    if (!s_warned_pressure && sPersistBytes >= PERSIST_MAX_BYTES
        && (sPersistLastFrame.mStalls > 0 || sPersistLastFrame.mBytes >= sPersistBytes))
    {
        s_warned_pressure = true;
        LL_WARNS("ShaderUniform")
            << "Persistent UBO ring under pressure at the size cap: " << sPersistLastFrame.mSlices
            << " slices / " << (sPersistLastFrame.mBytes >> 10) << " KiB in one frame ("
            << sPersistLastFrame.mSegmentSteps << " segment steps, " << sPersistLastFrame.mWraps
            << " full wraps, " << sPersistLastFrame.mStalls << " GPU stalls). The ring is "
            << (sPersistBytes >> 10) << " KiB (cap) in " << PERSIST_SEGMENTS
            << " segments; one frame's traffic met or exceeded the whole ring, or a fence stalled."
            << " Raise PERSIST_MAX_BYTES. Further frames at LL_DEBUGS(\"UBORing\")." << LL_ENDL;
    }

    // The scratch ring's blocking case is a fence stall (a wrap alone is routine: the
    // head walks continuously across frames, so a lap lands in SOME frame every few
    // frames even when healthy). Same warn-once idiom as above, same reason.
    static bool s_warned_scratch = false;
    if (!s_warned_scratch && sScratchLastFrame.mStalls > 0)
    {
        s_warned_scratch = true;
        LL_WARNS("ShaderUniform")
            << "Scratch UBO ring under pressure: " << sScratchLastFrame.mSlices
            << " payloads / " << (sScratchLastFrame.mBytes >> 10) << " KiB in one frame, "
            << sScratchLastFrame.mStalls << " segment-fence stalls ("
            << sScratchLastFrame.mStallTimeouts << " timeouts). The ring is "
            << (sScratchPersistBytes >> 10) << " KiB in " << SCRATCH_SEGMENTS
            << " segments; a stall blocks the frame on the GPU."
            << " Further frames at LL_DEBUGS(\"UBORing\")." << LL_ENDL;
    }

    // A within-frame FULL LAP of the persistent scratch ring is the silent-corruption signal a
    // stall does NOT catch. The scratch ring is upload-once / bind-per-use: a payload written
    // early this frame can be rebound by draws issued much later this frame. When the frame's
    // scratch traffic covers the whole ring (mBytes >= ring size, so by pigeonhole the head
    // revisited an offset it already wrote this frame), those early payloads are overwritten
    // in place while the late draws may still read them -- and the segment fence, recorded when
    // the segment was LEFT, does not gate a draw issued after that, so no stall is registered.
    // Below the cap this self-heals -- the adaptive sizing below grows the ring next frame -- so
    // only warn once the ring is already at SCRATCH_MAX_BYTES and STILL laps: then the CAP is too
    // small for this scene, which growth can't fix. (The orphan fallback -- sScratchPtr == null --
    // is exempt: it keeps the old store alive for in-flight draws on wrap.)
    static bool s_warned_scratch_lap = false;
    if (!s_warned_scratch_lap && sScratchPtr && sScratchPersistBytes >= SCRATCH_MAX_BYTES
        && sScratchLastFrame.mBytes >= sScratchPersistBytes)
    {
        s_warned_scratch_lap = true;
        LL_WARNS("ShaderUniform")
            << "Scratch UBO ring lapped WITHIN one frame at the size cap: " << sScratchLastFrame.mSlices
            << " payloads / " << (sScratchLastFrame.mBytes >> 10) << " KiB vs a "
            << (sScratchPersistBytes >> 10) << " KiB ring (cap). A payload written early this frame can be "
               "overwritten while draws issued later this frame still reference it (the segment fence "
               "does not gate them), risking intermittent skinning corruption. Raise SCRATCH_MAX_BYTES "
               "for this rigged-mesh load. Further frames at LL_DEBUGS(\"UBORing\")." << LL_ENDL;
    }

    // Per-frame detail for a profiling run. Cheap to leave in: DEBUGS is compiled to a
    // tag check, and the counters are plain increments on the flush paths.
    LL_DEBUGS("UBORing") << "persist (" << (sPersistBytes >> 10) << " KiB ring): " << sPersistLastFrame.mSlices << " slices, "
                         << sPersistLastFrame.mBytes << " B, " << sPersistLastFrame.mSegmentSteps
                         << " seg (left mask 0x" << std::hex << sPersistLastFrame.mSegmentMask << std::dec
                         << ", head seg " << sPersistSeg << "), " << sPersistLastFrame.mWraps << " wrap, "
                         << sPersistLastFrame.mStalls << " stall (" << sPersistLastFrame.mStallTimeouts
                         << " timeout), " << sPersistLastFrame.mChurnFlushes << " churn-reflush, "
                         << sPersistLastFrame.mLiveRebinds << " live-rebind"
                         << " | ring (" << (sRingBytes >> 10) << " KiB): " << sRingLastFrame.mSlices << " slices, "
                         << sRingLastFrame.mBytes << " B, " << sRingLastFrame.mWraps << " wrap"
                         << " | scratch (" << (sScratchPersistBytes >> 10) << " KiB ring): "
                         << sScratchLastFrame.mSlices << " payloads, "
                         << sScratchLastFrame.mBytes << " B, " << sScratchLastFrame.mSegmentSteps
                         << " seg (left mask 0x" << std::hex << sScratchLastFrame.mSegmentMask << std::dec
                         << "), " << sScratchLastFrame.mWraps << " wrap, "
                         << sScratchLastFrame.mStalls << " stall ("
                         << sScratchLastFrame.mStallTimeouts << " timeout), "
                         << sScratchLastFrame.mLiveRebinds << " bind-dedup"
                         << (sScratchPtr ? "" : " [orphan fallback]")
                         << LL_ENDL;

    sPersistStats = RingStats();
    sRingStats    = RingStats();
    sScratchStats = RingStats();

    // Adaptively size each live ring to the observed traffic (next_ring_size): grow at once to keep
    // >= SLACK frames of head slack, shrink back toward the traffic after a cooldown so leaving a
    // crowd reclaims memory without a fluctuating scene thrashing. A resize tears the ring down
    // (resize_* -> teardown_*) and the next use recreates it at the new size -- safe here at the
    // frame boundary: in-flight draws keep the old store alive via GL's deferred delete and the
    // generation bump makes every owner re-write. Floors are restored on cleanupClass.
    //
    // Scratch: an early-frame payload must survive (through all its passes + GPU latency) without
    // the head lapping it mid-frame. Persistent streaming: a left segment's fence must have signaled
    // before it's reused a lap later, and segments stay near one frame of slices so clean-block
    // churn stays low. UPDATE_RING orphans on wrap (correct even tiny), but each wrap orphans a
    // whole store kept a lap, so ~SLACK frames keeps wraps to about one every few frames.
    if (sScratchPtr)
    {
        const size_t next = next_ring_size(sScratchPersistBytes, sScratchLastFrame.mBytes,
                                           SCRATCH_MIN_BYTES, SCRATCH_MAX_BYTES, SCRATCH_SLACK_FRAMES, sScratchShrinkCd);
        if (next != sScratchPersistBytes) { resize_scratch_ring(next); }
    }
    if (sPersistOK)
    {
        const size_t next = next_ring_size(sPersistBytes, sPersistLastFrame.mBytes,
                                           PERSIST_MIN_BYTES, PERSIST_MAX_BYTES, PERSIST_SLACK_FRAMES, sPersistShrinkCd);
        if (next != sPersistBytes) { resize_persist_ring(next); }
    }
    if (sRing != 0)
    {
        const size_t next = next_ring_size(sRingBytes, sRingLastFrame.mBytes,
                                           RING_MIN_BYTES, RING_MAX_BYTES, RING_SLACK_FRAMES, sRingShrinkCd);
        if (next != sRingBytes) { resize_ring(next); }
    }
}

// static
const ALUniformBuffer::RingStats& ALUniformBuffer::lastFramePersistentStats()
{
    return sPersistLastFrame;
}

// static
const ALUniformBuffer::RingStats& ALUniformBuffer::lastFrameRingStats()
{
    return sRingLastFrame;
}

// static
const ALUniformBuffer::RingStats& ALUniformBuffer::lastFrameScratchStats()
{
    return sScratchLastFrame;
}

// static
S64 ALUniformBuffer::scratchWrite(const void* data, size_t bytes)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

    if (!data || bytes == 0 || bytes > SCRATCH_MAX_PAYLOAD)
    {
        return -1;
    }

    if (!ensure_scratch_ring())
    {
        return -1;
    }

    const size_t aligned = align_up(bytes, (size_t)sScratchAlign);

    if (sScratchPtr)
    {
        // Persistent ring: advance segments exactly as flushPersistent does -- fence the segment
        // being left; before reusing the next one, wait out any draws that read it a lap ago. A
        // full lap bumps sScratchGeneration, staling the per-skin cached offsets (owners re-write).
        // Segment size tracks the adaptive ring size (SCRATCH_SEGMENTS stays fixed).
        advance_fenced_ring(aligned, sScratchPersistBytes / SCRATCH_SEGMENTS, SCRATCH_SEGMENTS,
                            sScratchHead, sScratchSeg, sScratchFence,
                            sScratchGeneration, sScratchStats, "Scratch");

        // COHERENT mapping: the memcpy is the whole publish.
        memcpy(sScratchPtr + sScratchHead, data, bytes);

        const S64 offset = (S64)sScratchHead;
        sScratchHead += aligned;
        ++sScratchStats.mSlices;
        sScratchStats.mBytes += aligned;
        return offset;
    }

    if (sScratchHead + aligned > SCRATCH_BYTES)
    {
        // Wrapped: orphan the ring (in-flight draws keep reading the old store) and
        // invalidate every cached offset via the generation bump -- owners re-write
        // on their next use.
        buffer_data(sScratch, (GLsizeiptr)SCRATCH_BYTES, nullptr, GL_STREAM_DRAW);
        sScratchHead = 0;
        ++sScratchGeneration;
        ++sScratchStats.mWraps;
    }

    // Fresh, never-in-flight region: unsynchronized map skips the GPU sync.
    buffer_write_unsync(sScratch, (GLintptr)sScratchHead, (GLsizeiptr)bytes, data);

    const S64 offset = (S64)sScratchHead;
    sScratchHead += aligned;
    ++sScratchStats.mSlices;
    sScratchStats.mBytes += aligned;
    return offset;
}

// static
void ALUniformBuffer::scratchBindRange(U32 binding, S64 offset, size_t bytes)
{
    if (sScratch == 0 || offset < 0 || bytes == 0)
    {
        return;
    }
    if (binding == sScratchBoundBinding && offset == sScratchBoundOffset
        && bytes == sScratchBoundBytes && sScratchGeneration == sScratchBoundGen)
    {
        // This exact slice is already live at this point (see the memo's declaration
        // for why callers legitimately re-apply it).
        ++sScratchStats.mLiveRebinds;
        return;
    }
    glBindBufferRange(GL_UNIFORM_BUFFER, binding, sScratch, (GLintptr)offset, (GLsizeiptr)bytes);
    sScratchBoundBinding = binding;
    sScratchBoundOffset  = offset;
    sScratchBoundBytes   = bytes;
    sScratchBoundGen     = sScratchGeneration;
}

// static
U32 ALUniformBuffer::scratchGeneration()
{
    return sScratchGeneration;
}

void ALUniformBuffer::cleanupClass()
{
    // Free all three rings and restore their size floors (and shrink timers) so a fresh context
    // starts small again (the teardown helpers bump the generation so cached slices re-write).
    teardown_ring();
    sRingBytes = RING_MIN_BYTES;
    sRingShrinkCd = 0;

    teardown_scratch_ring();
    sScratchPersistBytes = SCRATCH_MIN_BYTES;
    sScratchShrinkCd = 0;

    teardown_persist_ring();
    sPersistBytes = PERSIST_MIN_BYTES;
    sPersistShrinkCd = 0;
}
