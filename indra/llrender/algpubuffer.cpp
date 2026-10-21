/**
 * @file algpubuffer.cpp
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

#include "linden_common.h"

#include "algpubuffer.h"

#include "alframefences.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llrender.h"

#include <cstdint>
#include <cstring>
#include <vector>

U64 ALGPUBuffer::sBytesAllocated = 0;

namespace
{
    // The named forms of everything below but the clear, which has its own check. mHasDirectStateAccess vouches for
    // the creation, upload and flush calls; the copy and the read are resolved beside them but not checked with them.
    bool use_dsa()
    {
        return gGLManager.mHasDirectStateAccess && glCopyNamedBufferSubData && glGetNamedBufferSubData;
    }

    // Whether [offset, offset + bytes) lies in a store of `size` bytes, without the sum wrapping.
    bool in_range(size_t offset, size_t bytes, size_t size)
    {
        return offset <= size && bytes <= size - offset;
    }

    size_t round_up(size_t value, size_t multiple)
    {
        return (value + multiple - 1) / multiple * multiple;
    }

    // A store that stays mapped: GL 4.4's immutable storage, and a map of it, by name where DSA makes the store.
    bool can_map_persistent(bool dsa)
    {
        return glBufferStorage && (!dsa || (glNamedBufferStorage && glMapNamedBufferRange));
    }

    // Make a store of `size` bytes in buffer `name`, mapped persistently with `map_flags` unless they are 0. Returns the
    // map, or null where none was asked for or GL gave none.
    U8* make_store(GLuint name, bool dsa, GLsizeiptr size, const void* data, GLbitfield storage_flags,
                   GLbitfield map_flags, GLenum usage)
    {
        void* mapped = nullptr;
        if (glBufferStorage)
        {
            if (dsa && glNamedBufferStorage)
            {
                glNamedBufferStorage(name, size, data, storage_flags);
                if (map_flags)
                {
                    mapped = glMapNamedBufferRange(name, 0, size, map_flags);
                }
            }
            else
            {
                glBindBuffer(GL_COPY_WRITE_BUFFER, name);
                glBufferStorage(GL_COPY_WRITE_BUFFER, size, data, storage_flags);
                if (map_flags)
                {
                    mapped = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, size, map_flags);
                }
                glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            }
        }
        else if (dsa)
        {
            glNamedBufferData(name, size, data, usage);
        }
        else
        {
            glBindBuffer(GL_COPY_WRITE_BUFFER, name);
            glBufferData(GL_COPY_WRITE_BUFFER, size, data, usage);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        }
        return static_cast<U8*>(mapped);
    }
}

ALGPUBuffer::~ALGPUBuffer()
{
    release();
}

ALGPUBuffer::ALGPUBuffer(ALGPUBuffer&& other) noexcept
    : mName(other.mName)
    , mSize(other.mSize)
    , mFlags(other.mFlags)
    , mMapped(other.mMapped)
{
    other.mName   = 0;
    other.mSize   = 0;
    other.mFlags  = 0;
    other.mMapped = nullptr;
}

ALGPUBuffer& ALGPUBuffer::operator=(ALGPUBuffer&& other) noexcept
{
    if (this != &other)
    {
        release();
        mName   = other.mName;
        mSize   = other.mSize;
        mFlags  = other.mFlags;
        mMapped = other.mMapped;
        other.mName   = 0;
        other.mSize   = 0;
        other.mFlags  = 0;
        other.mMapped = nullptr;
    }
    return *this;
}

bool ALGPUBuffer::allocate(size_t bytes, U32 flags, const void* data, const char* label)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

    release();

    if (bytes == 0)
    {
        LL_WARNS("GPUBuffer") << "Refusing to allocate an empty buffer" << (label ? ": " : "") << (label ? label : "")
                              << LL_ENDL;
        llassert(false);
        return false;
    }

    // Whatever was pending is not this allocation's to report.
    clear_glerror();

    const bool dsa = use_dsa();
    auto create_name = [dsa]()
    {
        GLuint name = 0;
        if (dsa)
        {
            glCreateBuffers(1, &name);
        }
        else
        {
            glGenBuffers(1, &name);
        }
        return name;
    };

    GLuint name = create_name();
    if (name == 0)
    {
        LL_WARNS("GPUBuffer") << "No buffer name for " << (label ? label : "a GPU buffer") << LL_ENDL;
        return false;
    }

    // A flushed write map needs a write map, and a map that reads what the GPU wrote stays coherent.
    llassert(!(flags & FLUSH_EXPLICIT) || ((flags & STREAM) && !(flags & READBACK)));
    if (!(flags & STREAM) || (flags & READBACK))
    {
        flags &= ~FLUSH_EXPLICIT;
    }

    // Immutable where it resolved: the driver knows the store never moves, and a store only allocate() replaces is
    // all this class offers anyway. A STREAM store that is not mapped is uploaded to, as a DYNAMIC one is. The mutable
    // fallback's hint says who reads it: the CPU for READBACK, the GPU, from the CPU's writes or its own, otherwise.
    const GLsizeiptr size     = (GLsizeiptr)bytes;
    const bool       readback = (flags & READBACK) != 0;
    const bool       stream   = (flags & STREAM) != 0;
    const GLenum     usage    = readback ? GL_STREAM_READ
                              : stream   ? GL_STREAM_DRAW
                              : (flags & DYNAMIC) ? GL_DYNAMIC_DRAW : GL_DYNAMIC_COPY;
    const GLbitfield dynamic  = (flags & DYNAMIC) ? GL_DYNAMIC_STORAGE_BIT : 0;

    U8* mapped = nullptr;
    if ((readback || stream) && can_map_persistent(dsa))
    {
        // Coherent both ways, so neither side flushes ranges or issues client barriers: the frame fences the rings wait
        // on are all the ordering they need. FLUSH_EXPLICIT gives that up on the write side, where flushMapped()
        // publishes instead; the storage takes no flush bit, only the map. A readback store asks for memory the CPU
        // reads quickly.
        const GLbitfield coherency = (flags & FLUSH_EXPLICIT) ? GL_MAP_FLUSH_EXPLICIT_BIT : GL_MAP_COHERENT_BIT;
        const GLbitfield map_flags = GL_MAP_PERSISTENT_BIT | coherency
                                   | (readback ? GL_MAP_READ_BIT : 0) | (stream ? GL_MAP_WRITE_BIT : 0);
        const GLbitfield storage_flags = (map_flags & ~GL_MAP_FLUSH_EXPLICIT_BIT) | dynamic
                                       | (readback ? GL_CLIENT_STORAGE_BIT : 0);
        mapped = make_store(name, dsa, size, data, storage_flags, map_flags, usage);
        if (!mapped)
        {
            // An immutable store cannot be given another, so a map that failed starts over unmapped.
            LL_WARNS("GPUBuffer") << "Could not map " << (label ? label : "a GPU buffer") << "; using it unmapped"
                                  << LL_ENDL;
            glDeleteBuffers(1, &name);
            clear_glerror();
            name = create_name();
            if (name == 0)
            {
                return false;
            }
        }
    }
    if (!mapped)
    {
        make_store(name, dsa, size, data, dynamic | (stream ? GL_DYNAMIC_STORAGE_BIT : 0), 0, usage);
    }

    if (const GLenum error = glGetError(); error != GL_NO_ERROR)
    {
        LL_WARNS("GPUBuffer") << "Could not allocate " << bytes << " bytes for " << (label ? label : "a GPU buffer")
                              << " (GL error 0x" << std::hex << error << std::dec << ")" << LL_ENDL;
        glDeleteBuffers(1, &name);
        return false;
    }

    if (label && gDebugGL && gGLManager.mHasDebugOutput && glObjectLabel)
    {
        glObjectLabel(GL_BUFFER, name, -1, label);
    }

    mName   = name;
    mSize   = bytes;
    mFlags  = flags;
    mMapped = mapped;
    sBytesAllocated += bytes;
    LL_PROFILE_PLOT("GPU buffer bytes", (int64_t)sBytesAllocated);
    return true;
}

void ALGPUBuffer::release()
{
    if (mName != 0)
    {
        // Only issue GL while the context is alive: the destructor can run during shutdown after GL teardown, when
        // the name is already gone with the context. The same gate as ALUniformBuffer::release. Deleting a mapped
        // store unmaps it.
        if (gGLManager.mInited)
        {
            gGL.forgetBuffer(mName);
            glDeleteBuffers(1, &mName);
        }
        sBytesAllocated -= mSize;
        LL_PROFILE_PLOT("GPU buffer bytes", (int64_t)sBytesAllocated);
        mName = 0;
    }
    mSize   = 0;
    mFlags  = 0;
    mMapped = nullptr;
}

void ALGPUBuffer::upload(size_t offset, const void* data, size_t bytes)
{
    llassert(mFlags & (DYNAMIC | STREAM));
    if (!mName || !data || !in_range(offset, bytes, mSize))
    {
        llassert(false);
        return;
    }
    if (bytes == 0)
    {
        return;
    }

    if (mMapped && (mFlags & STREAM))
    {
        memcpy(mMapped + offset, data, bytes);
        flushMapped(offset, bytes);
    }
    else if (use_dsa())
    {
        glNamedBufferSubData(mName, (GLintptr)offset, (GLsizeiptr)bytes, data);
    }
    else
    {
        glBindBuffer(GL_COPY_WRITE_BUFFER, mName);
        glBufferSubData(GL_COPY_WRITE_BUFFER, (GLintptr)offset, (GLsizeiptr)bytes, data);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }
}

void ALGPUBuffer::flushMapped(size_t offset, size_t bytes)
{
    if (!mMapped || !(mFlags & FLUSH_EXPLICIT) || bytes == 0)
    {
        return;
    }
    if (!in_range(offset, bytes, mSize))
    {
        llassert(false);
        return;
    }

    // The flush is the mapping's, however the map was made.
    if (use_dsa())
    {
        glFlushMappedNamedBufferRange(mName, (GLintptr)offset, (GLsizeiptr)bytes);
    }
    else
    {
        glBindBuffer(GL_COPY_WRITE_BUFFER, mName);
        glFlushMappedBufferRange(GL_COPY_WRITE_BUFFER, (GLintptr)offset, (GLsizeiptr)bytes);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }
}

void ALGPUBuffer::clear(U32 value)
{
    clearRange(0, mSize, value);
}

void ALGPUBuffer::clearRange(size_t offset, size_t bytes, U32 value)
{
    llassert(offset % sizeof(U32) == 0 && bytes % sizeof(U32) == 0);
    if (!mName || !in_range(offset, bytes, mSize))
    {
        llassert(false);
        return;
    }
    if (bytes == 0)
    {
        return;
    }

    // Each word R32UI, filled from one GL_RED_INTEGER uint: the value's bits as they are, whatever the store holds.
    if (gGLManager.mHasDirectStateAccess && glClearNamedBufferSubData)
    {
        glClearNamedBufferSubData(mName, GL_R32UI, (GLintptr)offset, (GLsizeiptr)bytes, GL_RED_INTEGER,
                                  GL_UNSIGNED_INT, &value);
    }
    else if (glClearBufferSubData)
    {
        glBindBuffer(GL_COPY_WRITE_BUFFER, mName);
        glClearBufferSubData(GL_COPY_WRITE_BUFFER, GL_R32UI, (GLintptr)offset, (GLsizeiptr)bytes, GL_RED_INTEGER,
                             GL_UNSIGNED_INT, &value);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }
    else
    {
        // Below 4.3 the pattern goes up from here, which an immutable store takes only as DYNAMIC or STREAM.
        llassert(mFlags & (DYNAMIC | STREAM));
        const std::vector<U32> pattern(bytes / sizeof(U32), value);
        upload(offset, pattern.data(), bytes);
    }
}

void ALGPUBuffer::copyFrom(const ALGPUBuffer& src, size_t src_offset, size_t dst_offset, size_t bytes)
{
    if (!mName || !src.mName || !in_range(src_offset, bytes, src.mSize) || !in_range(dst_offset, bytes, mSize))
    {
        llassert(false);
        return;
    }
    // GL refuses a copy within one buffer whose ranges overlap.
    llassert(&src != this || src_offset + bytes <= dst_offset || dst_offset + bytes <= src_offset);
    if (bytes == 0)
    {
        return;
    }

    if (use_dsa())
    {
        glCopyNamedBufferSubData(src.mName, mName, (GLintptr)src_offset, (GLintptr)dst_offset, (GLsizeiptr)bytes);
    }
    else
    {
        glBindBuffer(GL_COPY_READ_BUFFER, src.mName);
        glBindBuffer(GL_COPY_WRITE_BUFFER, mName);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, (GLintptr)src_offset, (GLintptr)dst_offset,
                            (GLsizeiptr)bytes);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
    }
}

bool ALGPUBuffer::read(size_t offset, void* out, size_t bytes) const
{
    if (!mName || !out || !in_range(offset, bytes, mSize))
    {
        llassert(false);
        return false;
    }
    if (bytes == 0)
    {
        return true;
    }

    // A shader's writes reach a read back only past this barrier, and compute in use is the only way a shader can
    // have written here.
    if (LLRender::sCompute)
    {
        gGL.memoryBarrier(LLRender::BARRIER_BUFFER_UPDATE);
    }

    // glGetBufferSubData takes a persistently mapped store too, and waits for the GPU where a read of the map would not.
    if (use_dsa())
    {
        glGetNamedBufferSubData(mName, (GLintptr)offset, (GLsizeiptr)bytes, out);
    }
    else
    {
        glBindBuffer(GL_COPY_READ_BUFFER, mName);
        glGetBufferSubData(GL_COPY_READ_BUFFER, (GLintptr)offset, (GLsizeiptr)bytes, out);
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
    }
    return true;
}

//-----------------------------------------------------------------------------
// ALGPUReadback
//-----------------------------------------------------------------------------

ALGPUReadback::~ALGPUReadback()
{
    release();
}

bool ALGPUReadback::allocate(size_t bytes, U32 slots, const char* label)
{
    release();
    if (bytes == 0 || slots == 0)
    {
        llassert(false);
        return false;
    }

    const size_t stride = round_up(bytes, sizeof(U32));
    if (!mStore.allocate(stride * slots, ALGPUBuffer::READBACK, nullptr, label))
    {
        return false;
    }
    mSlots.assign(slots, Slot());
    mSlotBytes  = bytes;
    mSlotStride = stride;
    return true;
}

ALFrameFences& ALGPUReadback::frames() const
{
    return mFrameFences ? *mFrameFences : gGL.frameFences();
}

void ALGPUReadback::release()
{
    mSlots.clear();
    mStore.release();
    mSlotBytes  = 0;
    mSlotStride = 0;
    mNext       = 0;
    mPending    = 0;
}

bool ALGPUReadback::request(const ALGPUBuffer& src, size_t src_offset, size_t bytes)
{
    if (!mStore.isAllocated() || !src.isAllocated() || bytes == 0 || bytes > mSlotBytes
        || !in_range(src_offset, bytes, src.size()))
    {
        llassert(false);
        return false;
    }
    if (mPending == (U32)mSlots.size())
    {
        return false;
    }

    // The copy sees what shaders wrote to `src` only past this barrier.
    if (LLRender::sCompute)
    {
        gGL.memoryBarrier(LLRender::BARRIER_BUFFER_UPDATE);
    }

    mStore.copyFrom(src, src_offset, (size_t)mNext * mSlotStride, bytes);
    issueNext(bytes);
    return true;
}

bool ALGPUReadback::requestPixels(S32 x, S32 y, S32 width, S32 height, U32 format, U32 type, size_t bytes)
{
    if (!mStore.isAllocated() || width <= 0 || height <= 0 || bytes == 0 || bytes > mSlotBytes)
    {
        llassert(false);
        return false;
    }
    if (mPending == (U32)mSlots.size())
    {
        return false;
    }

    // Through the pack buffer binding, which nothing else in the renderer caches, and back to none after: while a
    // buffer is bound there, a read writes into it at the offset it is given in place of a pointer.
    const uintptr_t offset = (uintptr_t)mNext * mSlotStride;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, mStore.getGLName());
    glReadPixels(x, y, width, height, format, type, reinterpret_cast<void*>(offset));
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    issueNext(bytes);
    return true;
}

void ALGPUReadback::issueNext(size_t bytes)
{
    Slot& slot = mSlots[mNext];
    slot.mFrame = frames().frame();
    slot.mBytes = bytes;

    mNext = (mNext + 1) % (U32)mSlots.size();
    ++mPending;
}

bool ALGPUReadback::collect(void* out, size_t bytes, bool wait)
{
    if (mPending == 0 || !out)
    {
        return false;
    }

    const U32 count  = (U32)mSlots.size();
    const U32 oldest = (mNext + count - mPending) % count;
    Slot& slot = mSlots[oldest];
    llassert(bytes <= slot.mBytes);

    ALFrameFences& fences = frames();
    if (!fences.isComplete(slot.mFrame))
    {
        if (!wait)
        {
            return false;
        }
        // For the open frame this waits for every command issued so far, the copy among them.
        while (!fences.wait(slot.mFrame, 1000000000ull /* 1s */))
        {
        }
    }

    const size_t offset = (size_t)oldest * mSlotStride;
    const size_t copied = llmin(bytes, slot.mBytes);
    if (mStore.getMapped())
    {
        memcpy(out, mStore.getMapped() + offset, copied);
    }
    else
    {
        mStore.read(offset, out, copied);
    }
    --mPending;
    return true;
}

//-----------------------------------------------------------------------------
// ALGPUStream
//-----------------------------------------------------------------------------

ALGPUStream::~ALGPUStream()
{
    release();
}

bool ALGPUStream::allocate(size_t bytes, U32 regions, const char* label)
{
    release();
    if (bytes == 0 || regions == 0)
    {
        llassert(false);
        return false;
    }

    // Each region starts where a storage range may: the context's alignment, or 256 -- the most any context asks --
    // where it has given none.
    const size_t alignment = gGLManager.mShaderStorageBufferOffsetAlignment > 0
                           ? (size_t)gGLManager.mShaderStorageBufferOffsetAlignment
                           : 256;
    const size_t stride = round_up(round_up(bytes, sizeof(U32)), alignment);
    if (!mStore.allocate(stride * regions, ALGPUBuffer::STREAM, nullptr, label))
    {
        return false;
    }
    mReadIn.assign(regions, 0);
    if (!mStore.getMapped())
    {
        mStaging.resize(bytes);
    }
    mRegionBytes  = bytes;
    mRegionStride = stride;
    return true;
}

ALFrameFences& ALGPUStream::frames() const
{
    return mFrameFences ? *mFrameFences : gGL.frameFences();
}

void ALGPUStream::release()
{
    mReadIn.clear();
    mStaging.clear();
    mStore.release();
    mRegionBytes  = 0;
    mRegionStride = 0;
    mPublished    = -1;
    mWriting      = -1;
    mNext         = 0;
    mStalls       = 0;
}

U8* ALGPUStream::map()
{
    if (!mStore.isAllocated())
    {
        llassert(false);
        return nullptr;
    }

    // A region mapped and not yet published is the one to write still.
    if (mWriting < 0)
    {
        // Every read of the region published last was issued before this call, in this frame or an earlier one.
        ALFrameFences& fences = frames();
        if (mPublished >= 0)
        {
            mReadIn[mPublished] = fences.frame();
            mPublished = -1;
        }

        const U32 region = mNext;
        mNext = (mNext + 1) % (U32)mReadIn.size();
        // Finished already, unless the GPU is as many frames behind as there are regions, or the ring came round
        // within the frame: then the wait, which polls first, waits for every command of that frame.
        if (!fences.isComplete(mReadIn[region]))
        {
            bool stalled = false;
            if (!fences.wait(mReadIn[region], 1000000000ull /* 1s */, &stalled))
            {
                LL_WARNS_ONCE("GPUBuffer") << "A stream region was still in use after a second; writing it anyway"
                                           << LL_ENDL;
            }
            if (stalled)
            {
                ++mStalls;
            }
        }
        mReadIn[region] = 0;
        mWriting = (S32)region;
    }

    return mStore.getMapped() ? mStore.getMapped() + (size_t)mWriting * mRegionStride : mStaging.data();
}

size_t ALGPUStream::publish(size_t bytes)
{
    llassert(mWriting >= 0 && bytes <= mRegionBytes);
    if (mWriting < 0)
    {
        return 0;
    }
    bytes = llmin(bytes, mRegionBytes);

    const size_t offset = (size_t)mWriting * mRegionStride;
    if (!mStore.getMapped() && bytes > 0)
    {
        mStore.upload(offset, mStaging.data(), bytes);
    }
    mPublished = mWriting;
    mWriting   = -1;
    return offset;
}
