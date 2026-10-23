/**
 * @file altransienttargetpool.cpp
 * @brief Render targets lent by description for one pass or effect, and kept for the next that asks for the same.
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

#include "altransienttargetpool.h"

#include "llglheaders.h"
#include "llglstates.h"
#include "llimagegl.h"
#include "llrender.h"

#include <fmt/format.h>

#include <algorithm>
#include <iterator>

namespace
{
    using Pool = ALTransientTargetPool;
    const LLColor4 POISON_COLOUR(Pool::POISON_COLOUR);

    // LLRenderTarget's own GL work.
    class GLBacking final : public ALTransientTargetPool::Backing
    {
    public:
        bool build(LLRenderTarget& target, const ALTransientTargetPool::Key& key, std::string_view name) override
        {
            target.setLabel(name);
            const LLRenderTarget::EnsureResult result =
                key.mDepthFrom ? target.ensure(key.mDesc, *key.mDepthFrom) : target.ensure(key.mDesc);
            // Building binds each new texture to unit 0 to allocate it.
            gGL.getTextureSlot(0)->unbind();
            return result == LLRenderTarget::EnsureResult::Built;
        }

        void rename(LLRenderTarget& target, std::string_view name) override { target.setLabel(name); }

        void poison(LLRenderTarget& target, const ALTransientTargetPool::Key& key) override
        {
            const ALRenderTargetDesc& desc  = key.mDesc;
            const bool                depth = desc.depth != ALRenderTargetDesc::Depth::None;

            // A texture cleared as a texture, where GL 4.4 can: whatever framebuffer, masks and scissor are in force.
            if (glClearTexImage)
            {
                for (U32 attachment = 0; attachment < desc.colorCount(); ++attachment)
                {
                    if (!LLImageGL::isIntegerFormat(desc.color[attachment]))
                    {
                        glClearTexImage(target.getTexture(attachment), 0, GL_RGBA, GL_FLOAT, POISON_COLOUR.mV);
                    }
                }
                if (depth && !desc.stencil)
                {
                    glClearTexImage(target.getDepth(), 0, GL_DEPTH_COMPONENT, GL_FLOAT, &Pool::POISON_DEPTH);
                }
                else if (depth)
                {
                    // Given as a float depth and a stencil byte for D24S8 as for D32F_S8: AMD's driver writes the
                    // 24-bit integer of GL_UNSIGNED_INT_24_8 into the depth without normalising it, and drops the
                    // stencil.
                    const struct
                    {
                        F32 depth;
                        U32 stencil;
                    } value{ Pool::POISON_DEPTH, Pool::POISON_STENCIL };
                    glClearTexImage(target.getDepth(), 0, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, &value);
                }
                return;
            }

            // Otherwise through its framebuffer, every mask open and the scissor off, and the state put back.
            LLGLDisable    no_scissor(GL_SCISSOR_TEST);
            LLGLSColorMask colour_mask(true, true);
            LLGLDepthTest  depth_mask(GL_TRUE, GL_TRUE, GL_ALWAYS);
            GLint          clear_stencil = 0;
            GLfloat        clear_depth   = 1.f;
            glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &clear_stencil);
            glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clear_depth);
            const LLColor4 clear_colour = gGL.getClearColor();
            const U32      stencil_mask = gGL.getStencilMask();

            gGL.setStencilMask(0xff);
            glClearDepth(Pool::POISON_DEPTH);
            glClearStencil(Pool::POISON_STENCIL);
            gGL.setClearColor(POISON_COLOUR);
            target.bindTarget();
            // A shared depth is the owner's to keep.
            target.clear(depth ? 0xffffffff : GL_COLOR_BUFFER_BIT);
            target.flush();
            gGL.setClearColor(clear_colour);
            glClearStencil(clear_stencil);
            glClearDepth(clear_depth);
            gGL.setStencilMask(stencil_mask);
        }

        void free(LLRenderTarget& target) override { target.release(); }

        U64 depthSerial(const LLRenderTarget& owner) const override { return owner.getDepthSerial(); }
    };

    GLBacking& gl_backing()
    {
        static GLBacking backing;
        return backing;
    }
}

ALTransientTargetPool::ALTransientTargetPool() : ALTransientTargetPool(gl_backing())
{
}

ALTransientTargetPool::ALTransientTargetPool(Backing& backing) : mBacking(backing)
{
}

ALTransientTargetPool::~ALTransientTargetPool()
{
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        mBacking.free(entry->mTarget);
    }
}

LLRenderTarget* ALTransientTargetPool::acquire(const ALRenderTargetDesc& desc, std::string_view name, U32 trim_frames)
{
    return acquire(Key{ .mDesc = desc }, name, trim_frames);
}

LLRenderTarget* ALTransientTargetPool::acquire(const ALRenderTargetDesc& desc, const LLRenderTarget& depth_from,
                                               std::string_view name, U32 trim_frames)
{
    return acquire(Key{ .mDesc = desc, .mDepthFrom = &depth_from, .mDepthSerial = mBacking.depthSerial(depth_from) },
                   name, trim_frames);
}

LLRenderTarget* ALTransientTargetPool::acquire(const Key& key, std::string_view name, U32 trim_frames)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DISPLAY;

    // Of the free targets that are what is asked, the one used last: the others go unused the longer, and are trimmed
    // the sooner, where the frame needs fewer of them than it did.
    Entry* match = nullptr;
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        if (!entry->mHeld && !entry->mDoomed && entry->mKey == key && (!match || entry->mLastUsed > match->mLastUsed))
        {
            match = entry.get();
        }
    }

    if (match)
    {
        ++mThisFrame.mHits;
        // Its holder's name, which a leak is reported under; debuggers are told it where the pool validates.
        if (match->mName != name)
        {
            match->mName = name;
        }
        if (mValidation && match->mLabel != name)
        {
            match->mLabel = name;
            mBacking.rename(match->mTarget, name);
        }
    }
    else
    {
        auto entry  = std::make_unique<Entry>();
        entry->mKey = key;
        if (!mBacking.build(entry->mTarget, key, name))
        {
            ++mThisFrame.mFailures;
            LL_WARNS("RenderResources") << "Transient target '" << name << "' could not be built at " << key.mDesc.width
                                        << "x" << key.mDesc.height << LL_ENDL;
            mBacking.free(entry->mTarget);
            return nullptr;
        }
        ++mThisFrame.mMisses;
        entry->mName  = name;
        entry->mLabel = name;
        entry->mBytes = LLRenderTarget::bytesFor(key.mDesc);
        mBytes += entry->mBytes;
        match = entry.get();
        mEntries.push_back(std::move(entry));
    }

    match->mHeld       = true;
    match->mLastUsed   = mFrame;
    match->mLease      = ++mLeases;
    match->mTrimFrames = std::max({ match->mTrimFrames, trim_frames, mKeepAtLeast });
    if (mRecording)
    {
        if (!match->mServed.empty())
        {
            match->mServed += ", ";
        }
        match->mServed += name;
    }
    return &match->mTarget;
}

bool ALTransientTargetPool::release(LLRenderTarget* target)
{
    return release(target, lease(target));
}

bool ALTransientTargetPool::release(LLRenderTarget* target, U64 lease)
{
    Entry* entry = find(target);
    if (!entry || !entry->mHeld || entry->mLease != lease)
    {
        noteBadRelease(target);
        return false;
    }

    entry->mHeld     = false;
    entry->mLastUsed = mFrame;
    if (entry->mDoomed)
    {
        entry->mFree = true;
        sweep();
        return true;
    }
    if (mValidation)
    {
        mBacking.poison(entry->mTarget, entry->mKey);
    }
    return true;
}

void ALTransientTargetPool::noteBadRelease(const LLRenderTarget* target)
{
    ++mBadReleases;
    const Entry* entry = find(target);
    LL_WARNS("RenderResources") << "Transient target '" << (entry ? std::string_view(entry->mName) : std::string_view("unknown"))
                                << "' released "
                                << (entry ? "again, by a scope that never acquired it, or by one whose loan of it was taken "
                                            "back as a leak"
                                          : "but never acquired")
                                << LL_ENDL;
}

void ALTransientTargetPool::beginFrame()
{
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        if (entry->mHeld)
        {
            ++mLeaks;
            LL_WARNS("RenderResources") << "Transient target '" << entry->mName << "' held past the end of a frame"
                                        << LL_ENDL;
            entry->mHeld = false;
            entry->mFree = entry->mDoomed;
        }
    }

    mNamesRecorded = mRecording;
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        entry->mLastServed = mRecording ? std::move(entry->mServed) : std::string();
        entry->mServed.clear();
    }
    mRecording  = mRecordNext;
    mRecordNext = false;

    mLastFrame = stats();
    mThisFrame = {};
    ++mFrame;

    // Freed once as many whole frames as it is kept through have gone by since the one it was last used in. While
    // trimming is held, every target counts as used in this one.
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        if (mTrimHeld)
        {
            entry->mLastUsed = mFrame;
        }
        entry->mFree = entry->mFree || mFrame - entry->mLastUsed > entry->mTrimFrames;
    }
    mThisFrame.mTrimmed = sweep();
}

void ALTransientTargetPool::setMainSize(U32 width, U32 height)
{
    if (width != mMainWidth || height != mMainHeight)
    {
        mMainWidth  = width;
        mMainHeight = height;
        clear();
    }
}

void ALTransientTargetPool::dropStaleDepth()
{
    // A doomed target's owner may be gone already: it is freed as it is released, whatever its depth.
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        if (!entry->mDoomed && entry->mKey.mDepthFrom &&
            mBacking.depthSerial(*entry->mKey.mDepthFrom) != entry->mKey.mDepthSerial)
        {
            drop(*entry);
        }
    }
    sweep();
}

void ALTransientTargetPool::trim(const ALRenderTargetDesc& desc)
{
    const Key key{ .mDesc = desc };
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        if (!entry->mHeld && entry->mKey == key)
        {
            entry->mFree = true;
        }
    }
    mThisFrame.mTrimmed += sweep();
}

void ALTransientTargetPool::clear()
{
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        drop(*entry);
    }
    sweep();
}

std::string ALTransientTargetPool::describe() const
{
    std::string text;
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        const ALRenderTargetDesc& desc = entry->mKey.mDesc;
        fmt::format_to(std::back_inserter(text), "{}{}x{}, {:.1f} MiB{}: {}", text.empty() ? "" : "\n", desc.width,
                       desc.height, (F64)entry->mBytes / (1024.0 * 1024.0), entry->mKey.mDepthFrom ? ", shared depth" : "",
                       !entry->mLastServed.empty() ? std::string_view(entry->mLastServed)
                       : mNamesRecorded            ? std::string_view("unused")
                                                   : std::string_view(entry->mName));
    }
    return text;
}

ALTransientTargetPool::Stats ALTransientTargetPool::stats() const
{
    Stats stats    = mThisFrame;
    stats.mBytes   = mBytes;
    stats.mTargets = (U32)mEntries.size();
    return stats;
}

U64 ALTransientTargetPool::lease(const LLRenderTarget* target) const
{
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        if (&entry->mTarget == target)
        {
            return entry->mHeld ? entry->mLease : 0;
        }
    }
    return 0;
}

ALTransientTargetPool::Entry* ALTransientTargetPool::find(const LLRenderTarget* target)
{
    for (const std::unique_ptr<Entry>& entry : mEntries)
    {
        if (&entry->mTarget == target)
        {
            return entry.get();
        }
    }
    return nullptr;
}

void ALTransientTargetPool::drop(Entry& entry)
{
    if (entry.mHeld)
    {
        entry.mDoomed = true;
    }
    else
    {
        entry.mFree = true;
    }
}

U32 ALTransientTargetPool::sweep()
{
    // A target over the depth of one that is freed goes with it: its framebuffer attaches that depth, so it could be
    // matched by no acquisition again, and its key names a target that is gone.
    for (bool spread = true; spread;)
    {
        spread = false;
        for (const std::unique_ptr<Entry>& sharer : mEntries)
        {
            if (sharer->mFree || sharer->mDoomed || !sharer->mKey.mDepthFrom)
            {
                continue;
            }
            for (const std::unique_ptr<Entry>& owner : mEntries)
            {
                if (owner->mFree && &owner->mTarget == sharer->mKey.mDepthFrom)
                {
                    drop(*sharer);
                    spread = true;
                    break;
                }
            }
        }
    }

    U32 freed = 0;
    for (size_t i = mEntries.size(); i-- > 0;)
    {
        Entry& entry = *mEntries[i];
        if (entry.mFree)
        {
            mBacking.free(entry.mTarget);
            mBytes -= entry.mBytes;
            mEntries.erase(mEntries.begin() + (std::ptrdiff_t)i);
            ++freed;
        }
    }
    return freed;
}
