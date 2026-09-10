/**
 * @file almotionpredictor.h
 * @brief The arithmetic behind viewer-side prediction of object motion between
 *        simulator updates
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#ifndef AL_MOTIONPREDICTOR_H
#define AL_MOTIONPREDICTOR_H

#include "stdtypes.h"
#include "v3math.h"

/**
 * Everything @ref LLViewerObject::interpolateLinearMotion has to decide, minus the viewer.
 *
 * Between updates the viewer carries an object forward along the velocity and acceleration the
 * simulator last sent. Two questions have to be answered every frame -- how far to step, and how
 * much to believe a prediction that is getting old -- and both were previously answered inline,
 * in a function that also walks regions, clamps to terrain and talks to the octree. Deliberately
 * free of viewer globals so @c almotionpredictor_test can exercise it directly.
 *
 * One instance lives on each @ref LLViewerObject, because the second question is answered from
 * that object's own history: see @ref phaseOutWindow.
 */
class ALMotionPredictor
{
public:
    /// The simulator steps its physics at 45 Hz. Update velocities are averages over one such
    /// step rather than the velocity at its end, so they need converting before they are
    /// integrated -- see @ref finalVelocity.
    static constexpr F32 SIM_TIMESTEP = 1.f / 45.f;

    /// Knobs, all in seconds except the two that are not.
    struct Tuning
    {
        /// Earliest a taper may begin, in seconds of silence. Floor for the cadence-derived start.
        F32  mPhaseOutTime  = 1.f;
        /// Where prediction has stopped entirely. Must exceed @c mPhaseOutTime to leave a ramp.
        F32  mMaxTime       = 3.f;
        /// Multiples of an object's own observed update interval before its taper starts.
        F32  mCadenceFactor = 4.f;
        /// Ceiling on the cadence-derived start, so a very slow mover still stops eventually.
        F32  mCadenceCap    = 10.f;
        /// Largest single step, seconds. Bounds what a stall can do when the viewer resumes.
        F32  mMaxFrameStep  = 0.5f;
        /// True opens the window on the object's own cadence. False, the default, ignores it: the
        /// window is then the fixed [mPhaseOutTime, mMaxTime], and the caller decides from the
        /// circuit whether it applies at all. Off until the cadence heuristic has been checked
        /// against what the simulator actually does with a ballistic object it has nothing to
        /// correct -- the very silence it would read as a fault.
        bool mCadenceAware  = false;
    };

    /// Where a taper runs from and to, in seconds since the last update.
    struct Window
    {
        F32 mStart = -1.f;
        F32 mEnd   = -1.f;

        /// A window only tapers if one was established. An object whose update rate has never
        /// been observed has no basis for calling silence a fault, and keeps coasting.
        bool isTapering() const { return mStart >= 0.f && mEnd > mStart; }
    };

    // -- observed update cadence ---------------------------------------------------------------

    /**
     * Record that an update was accepted at @p now_seconds.
     *
     * The gap since the previous one feeds an EWMA of this object's update interval. The first
     * call only sets the mark: one update is a timestamp, not a rate.
     */
    void noteUpdate(F64 now_seconds);

    /// Forget the observed rate, for when an object's update source changes (a region handoff).
    void forgetCadence();

    bool hasCadence() const  { return mUpdateInterval > 0.f; }
    F32  getUpdateInterval() const { return mUpdateInterval; }

    // -- decisions -----------------------------------------------------------------------------

    /**
     * The taper window for this object.
     *
     * The simulator stops sending updates for two quite different reasons: the prediction is
     * still right (a ballistic arc it has nothing to correct), or the updates are not arriving
     * (loss, a handoff, an interest-list drop). Only the second should stop the object, and what
     * separates them is the object's own history -- an object that had been updating at 10 Hz and
     * has said nothing for two seconds is in trouble; one that has never sent a second update is
     * not evidence of anything. So the window opens at a multiple of the observed interval, never
     * earlier than @c mPhaseOutTime and never later than @c mCadenceCap, and an object with no
     * observed cadence gets no window at all.
     */
    Window phaseOutWindow(const Tuning& tuning) const;

    /**
     * How much of the predicted step to keep, 1 down to 0 across @p window.
     *
     * One expression rather than a first-frame case and a continuing case, which is what the two
     * of them were: written separately they disagreed, and the switch between them put a step of
     * about a third into a curve whose entire purpose is to not have one.
     */
    static F32 phaseOutFactor(F32 time_since_update, const Window& window);

    /// Bound a single step. A frame that spans a stall -- a paused viewer, a long hitch -- would
    /// otherwise carry every moving object forward by the whole of it at once.
    static F32 clampFrameStep(F32 dt, const Tuning& tuning);

    // -- integration ---------------------------------------------------------------------------

    /**
     * Convert a reported velocity into the velocity at the end of the simulator's timestep.
     *
     * Applied once, when an update arrives, rather than folded into every frame of the
     * integration. Done per frame it depends on the viewer's frame time, and above 45 fps the
     * correction is larger than the step it corrects: the first frame of a fall then moves the
     * object *upward*, by about half a millimetre at 120 fps and not at all at 45.
     *
     * @see SIGN_AVERAGE_TO_FINAL for the size and direction of the offset.
     */
    static LLVector3 finalVelocity(const LLVector3& reported, const LLVector3& accel);

    /// Exact constant-acceleration step. Frame-size independent, and never opposes @p accel.
    static LLVector3 positionDelta(const LLVector3& vel, const LLVector3& accel, F32 dt);

private:
    /**
     * Direction of the average-to-final velocity conversion, in units of `0.5 * accel * timestep`.
     *
     * -1 reproduces what the viewer has always done. It is not obviously right: if a reported
     * velocity really is the mean over the last timestep then the true velocity *leads* it and
     * the offset should be +1, which is 44 mm of difference per 0.2 s of prediction under gravity.
     * The two are distinguishable by measuring predicted position against the next reported one,
     * which the "Interpolate" debug log now records -- so this stays where the viewer has it until
     * that reading says otherwise, and changing it is a one-character change here.
     */
    static constexpr F32 SIGN_AVERAGE_TO_FINAL = -1.f;

    /// Weight given to the newest interval. Low enough that one late packet does not redefine
    /// the object's rate, high enough to follow a sim that genuinely changes its send rate.
    static constexpr F32 CADENCE_SMOOTHING = 0.25f;

    /// Intervals outside this are not evidence about a send rate: below is a burst of updates in
    /// one frame, above is the silence the window exists to detect, and folding either back into
    /// the average would move the threshold to wherever the object already is.
    static constexpr F32 MIN_CREDIBLE_INTERVAL = SIM_TIMESTEP;
    static constexpr F32 MAX_CREDIBLE_INTERVAL = 2.5f;

    F64 mLastUpdate     = 0.0;
    F32 mUpdateInterval = 0.f;  ///< EWMA; <= 0 until a second update has been seen
};

#endif // AL_MOTIONPREDICTOR_H
