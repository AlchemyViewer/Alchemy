/**
 * @file altransienttargetpool.h
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

#pragma once

#include "alrendertargetdesc.h"
#include "llrendertarget.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/// Render targets lent for as long as a pass or an effect needs them, and kept for the next that asks for the same: a
/// target a frame needs for one phase of it shares its memory with every other of its description used at another
/// time, without anyone saying which. Passes take them through ALTransientTargets scopes.
///
/// A free target is handed out only for exactly the description it was built from, sharing exactly the depth it was
/// built against, so acquiring one never builds its storage or attaches a depth again: it is the same GL objects. What
/// it holds is undefined: every user clears it or writes all of it before reading it.
///
/// Nothing here waits on the GPU. GL carries out commands in the order they are issued, so a target one pass releases
/// can go to the next pass in the same frame: the GPU has finished the first pass's reads of it before it runs the
/// second's writes. A backend whose commands are not ordered so keeps a released target from reuse until the frame that
/// last used it has retired.
///
/// A target no one has acquired for TRIM_FRAMES frames, or for as many as an acquisition of it asked, is freed, and
/// every target when the main view changes size, which sizes nearly all of them.
class ALTransientTargetPool
{
public:
    /// How many whole frames a free target may go unacquired before it is freed, unless an acquisition of it asked for
    /// more: two seconds at 60 fps, which keeps a target through a feature switched off and on again, or an effect that
    /// runs every few frames, without holding a screen's worth of memory for long behind a feature left off.
    static constexpr U32 TRIM_FRAMES = 120;

    /// What a target's own attachments hold once it is released under validation: a colour no pass writes, and a depth
    /// halfway along either convention's range, where nothing the scene draws lands by chance.
    static constexpr F32 POISON_COLOUR[4] = { 1.f, 0.f, 1.f, 1.f };
    static constexpr F32 POISON_DEPTH     = 0.5f;
    static constexpr U8  POISON_STENCIL   = 0x5a;

    /// What a pooled target is, and so what an acquisition must ask for to be handed it: its own storage, and the
    /// target whose depth it shares, with the build of that depth it attached.
    struct Key
    {
        ALRenderTargetDesc    mDesc;
        const LLRenderTarget* mDepthFrom   = nullptr;
        U64                   mDepthSerial = 0;

        friend bool operator==(const Key&, const Key&) = default;
    };

    /// What building, poisoning and freeing a target is. LLRenderTarget's own GL work by default; a test's records
    /// stand in for it so that the policy runs without GL.
    class Backing
    {
    public:
        virtual ~Backing() = default;

        /// Make `target`, released, what `key` describes, named `name`; false leaves it released.
        virtual bool build(LLRenderTarget& target, const Key& key, std::string_view name) = 0;
        /// Give `target` the name `name` for debuggers.
        virtual void rename(LLRenderTarget& target, std::string_view name) = 0;
        /// Fill every attachment of its own with a value no pass writes, so that a read of it after its release shows.
        /// A depth it shares is another target's, and is left alone.
        virtual void poison(LLRenderTarget& target, const Key& key) = 0;
        /// Release `target`.
        virtual void free(LLRenderTarget& target) = 0;
        /// The build of its depth `owner` holds now: LLRenderTarget::getDepthSerial.
        virtual U64 depthSerial(const LLRenderTarget& owner) const = 0;
    };

    /// What the pool holds, and what one frame asked of it.
    struct Stats
    {
        U64 mBytes    = 0; ///< what its targets hold, counted as LLRenderTarget::sBytesAllocated counts it
        U32 mTargets  = 0; ///< how many targets it holds, acquired or free
        U32 mHits     = 0; ///< acquisitions a free target met
        U32 mMisses   = 0; ///< acquisitions that built one
        U32 mFailures = 0; ///< acquisitions that could not build one
        U32 mTrimmed  = 0; ///< targets freed for going unacquired
    };

    /// Builds through LLRenderTarget.
    ALTransientTargetPool();
    /// Builds through `backing`, which outlives the pool.
    explicit ALTransientTargetPool(Backing& backing);
    ~ALTransientTargetPool();

    ALTransientTargetPool(const ALTransientTargetPool&)            = delete;
    ALTransientTargetPool& operator=(const ALTransientTargetPool&) = delete;

    /// A target that is what `desc` describes, depth included, for the caller alone until it is released; null where
    /// none can be built. `name` is what debuggers call it, and what describe() lists it under. Released, it is kept
    /// through as many whole frames unacquired as the most any acquisition of it asked (`trim_frames`), and never
    /// fewer than TRIM_FRAMES: a user that draws seconds apart asks for more, to take it again without building it.
    LLRenderTarget* acquire(const ALRenderTargetDesc& desc, std::string_view name, U32 trim_frames = TRIM_FRAMES);
    /// A target that is what `desc` describes with `depth_from`'s depth attached. `desc` has no depth of its own and is
    /// depth_from's size. depth_from outlives every target the pool holds against it: the pool is cleared, or its
    /// targets against a depth built again dropped (dropStaleDepth), before it goes.
    LLRenderTarget* acquire(const ALRenderTargetDesc& desc, const LLRenderTarget& depth_from, std::string_view name,
                            U32 trim_frames = TRIM_FRAMES);
    /// Give `target` back. One the pool holds and has not lent is a release of a target released already, and one it
    /// does not hold a release of what it never lent: either is refused, counted (badReleases) and reported.
    bool release(LLRenderTarget* target);
    /// Give `target` back where it is lent under `lease` (lease()): one lent again since, after the pool took it back
    /// from a leak, is refused as the others are, and stays its new holder's.
    bool release(LLRenderTarget* target, U64 lease);
    /// The acquisition `target` is lent under while it is held, each a number of its own; 0 for one not held.
    U64 lease(const LLRenderTarget* target) const;

    /// Start a frame. A target still acquired from the last one is a leak: reported, counted (leaks) and taken back.
    /// Free targets no one has acquired for as many whole frames as each is kept through are freed, unless trimming is
    /// held. The frame's counts start again, those of the frame that ended kept as lastFrame().
    void beginFrame();

    /// While it lives, every acquisition from the pool asks to be kept through at least `frames` whole frames unacquired,
    /// as though each had asked it: for a caller whose callees acquire for it, as a preview drawn through the post chain
    /// does, or whose targets go unused seconds at a time.
    class Keep
    {
    public:
        Keep(ALTransientTargetPool& pool, U32 frames) : mPool(pool), mWas(pool.mKeepAtLeast)
        {
            pool.mKeepAtLeast = std::max(mWas, frames);
        }
        ~Keep() { mPool.mKeepAtLeast = mWas; }

        Keep(const Keep&)            = delete;
        Keep& operator=(const Keep&) = delete;

    private:
        ALTransientTargetPool& mPool;
        const U32              mWas;
    };

    /// While held, each frame begun counts as one every target was acquired in: none is freed for going unacquired, and
    /// once the hold ends each is kept as many whole frames more as it is kept through. The viewer holds it until the
    /// user is logged in, so that the frames of the login, which draw no world, keep what the reconcile readied for the
    /// world's first.
    void holdTrimming(bool hold) { mTrimHeld = hold; }

    /// The main view's render size, which sizes nearly every target the pool holds: where it differs from the last,
    /// every target is freed.
    void setMainSize(U32 width, U32 height);
    /// Free each target whose shared depth has been built again since it attached it, and which no acquisition can
    /// match any more.
    void dropStaleDepth();
    /// Free now the free targets that are what `desc` describes, sharing no depth, rather than once they have gone
    /// unused as long as they are kept through: for a caller that knows it will not ask for that description again.
    /// One held is left to its holder.
    void trim(const ALRenderTargetDesc& desc);
    /// Free every target. One still acquired is freed when it is released.
    void clear();

    // Whatever frees a target frees with it the targets that share its depth: their framebuffers attach it.

    /// Poison each target as it is released, and name each for whoever acquires it, not only whoever built it: the
    /// viewer asks for it under debug GL (gDebugGL, from RenderDebugGLSession or DebugSession).
    void setValidation(bool validation) { mValidation = validation; }
    bool validation() const { return mValidation; }

    /// Record the names each target serves over the next whole frame, from the next beginFrame to the one after, which
    /// keeps them for describe().
    void recordNames() { mRecordNext = true; }
    /// Whether the frame the last beginFrame ended recorded them.
    bool namesRecorded() const { return mNamesRecorded; }
    /// Every target the pool holds, a line each: its size and bytes, whether it shares a depth, and, where the last
    /// frame recorded them, the names it served then: "2560x1440, 28.1 MiB, shared depth: main.light, main.water".
    std::string describe() const;

    /// What the pool holds now, and this frame's counts so far.
    Stats stats() const;
    /// The counts of the frame the last beginFrame ended, with what the pool held then.
    const Stats& lastFrame() const { return mLastFrame; }

    U32 leaks() const { return mLeaks; }
    U32 badReleases() const { return mBadReleases; }
    /// Count a release of a target no scope held; for ALTransientTargets.
    void noteBadRelease(const LLRenderTarget* target);

    /// The frames begun so far.
    U32 frame() const { return mFrame; }

private:
    struct Entry
    {
        Key            mKey;
        LLRenderTarget mTarget;
        U64            mBytes    = 0;
        U32            mLastUsed = 0;
        /// The acquisition it was last lent under.
        U64            mLease      = 0;
        /// How many whole frames it is kept through unacquired: the most any acquisition of it asked.
        U32            mTrimFrames = TRIM_FRAMES;
        bool           mHeld     = false;
        /// Freed as it is released: a drop found it acquired.
        bool           mDoomed   = false;
        /// Freed by the next sweep.
        bool           mFree     = false;
        /// What whoever holds it, or held it last, calls it: what a leak or a bad release is reported under.
        std::string    mName;
        /// What debuggers were last told it is called: its builder's name, and under validation its holder's.
        std::string    mLabel;
        /// The names it served while names are recorded, comma-separated, and those of the frame last recorded.
        std::string    mServed;
        std::string    mLastServed;
    };

    LLRenderTarget* acquire(const Key& key, std::string_view name, U32 trim_frames);
    Entry*          find(const LLRenderTarget* target);
    /// Free `entry` at the next sweep, or as it is released where it is acquired.
    void            drop(Entry& entry);
    /// Free what is dropped and free, and what shares its depth; how many targets went.
    U32             sweep();

    Backing&                            mBacking;
    std::vector<std::unique_ptr<Entry>> mEntries;
    U64                                 mBytes       = 0;
    U64                                 mLeases      = 0;
    U32                                 mFrame       = 0;
    U32                                 mMainWidth   = 0;
    U32                                 mMainHeight  = 0;
    U32                                 mLeaks       = 0;
    U32                                 mBadReleases = 0;
    Stats                               mThisFrame;
    Stats                               mLastFrame;
    bool                                mValidation    = false;
    bool                                mTrimHeld      = false;
    /// The fewest whole frames every acquisition asks to be kept through, while a Keep lives.
    U32                                 mKeepAtLeast   = 0;
    bool                                mRecordNext    = false;
    bool                                mRecording     = false;
    bool                                mNamesRecorded = false;
};
