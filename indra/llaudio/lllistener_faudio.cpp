/**
 * @file lllistener_faudio.cpp
 * @brief Implementation of LLListener_FAudio (X3DAudio binding).
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
 *
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "lllistener_faudio.h"
#include "llaudioengine_faudio.h"

#include <FAudio.h>
#include <F3DAudio.h>

#include <array>
#include <cmath>

namespace
{
    // F3DAudio's matrix calculation does `Right = Top x Front`, which is the
    // left-handed convention. Linden is right-handed (+X forward, +Y left,
    // +Z up); pass the same vectors directly and pan ends up mirrored.
    // Map Linden -> X3DAudio left-handed (+X right, +Y up, +Z forward):
    //   x3d.x = -linden.y, x3d.y = linden.z, x3d.z = linden.x
    inline F3DAUDIO_VECTOR linden_to_x3d(const LLVector3& v)
    {
        return F3DAUDIO_VECTOR{ -v.mV[1], v.mV[2], v.mV[0] };
    }

    inline F3DAUDIO_VECTOR linden_to_x3d_unit(const LLVector3& v)
    {
        F32 len = v.length();
        if (len < 1e-6f)
        {
            // Sentinel forward when the input is degenerate. Used only for
            // listener basis vectors, which the viewer always passes valid.
            return F3DAUDIO_VECTOR{ 0.f, 0.f, 1.f };
        }
        F32 inv = 1.0f / len;
        return F3DAUDIO_VECTOR{ -v.mV[1] * inv, v.mV[2] * inv, v.mV[0] * inv };
    }

    // Audible range base distance (metres). The volume curve below is
    // normalized to a CurveDistanceScaler computed as kAudibleRange /
    // rolloff; with the default unit rolloff this puts the asymptote
    // (kVolumeCurvePoints[end].x = 1.0) at 384 m — roughly the diagonal
    // of a 256 x 256 m SL region. Fixed rather than user-tunable: the
    // earlier "audible range" knob exposed a setting that mostly drifted
    // away from FMOD parity when touched, and the curve table is tuned
    // assuming this value.
    constexpr float kAudibleRange = 384.0f;

    // Distance-attenuation curve. Tracks FMOD's FMOD_3D_INVERSEROLLOFF
    // with default min_distance = 1.0 m:
    //   volume = min_dist / (min_dist + rolloff * (d - min_dist))   for d > min_dist
    //   volume = 1.0                                                 for d <= min_dist
    //
    // FMOD's set3DRolloff factor changes the *shape* of the curve (sound
    // drops faster at higher factors) rather than the audible range, so
    // we apply rolloff by regenerating the curve points each time the
    // factor changes — not by scaling CurveDistanceScaler. The other
    // FAudio knobs that touch the curve (kAudibleRange) stay fixed.
    //
    // F3DAudio interpolates linearly between supplied points; sampling
    // at 10 points keeps worst-case interpolation error below the dB
    // resolution of typical hearing across the audible band.
    //
    // Beyond fractional 1.0, F3DAudio clamps to the last point's gain —
    // sources past the audible range settle at the curve's tail value
    // instead of continuing to drop. Negligible in practice (~-52 dB at
    // rolloff=1) and a clean stop-loss against extreme-distance
    // ghosting.
    constexpr float kVolumeCurveMinDistance = 1.0f;
    constexpr float kVolumeCurveSampleDistances[] = {
        0.0f, 1.0f, 1.5f, 3.0f, 7.5f, 15.0f, 38.0f, 75.0f, 150.0f, 384.0f
    };
    constexpr std::size_t kVolumeCurvePointCount =
        sizeof(kVolumeCurveSampleDistances) /
        sizeof(kVolumeCurveSampleDistances[0]);

    F3DAUDIO_DISTANCE_CURVE_POINT kVolumeCurvePoints[kVolumeCurvePointCount];
    F3DAUDIO_DISTANCE_CURVE kVolumeCurve = {
        kVolumeCurvePoints,
        static_cast<uint32_t>(kVolumeCurvePointCount)
    };

    // Regenerate kVolumeCurvePoints for the given rolloff factor. Main
    // thread only — FAudio's mixer thread reads matrices written by
    // F3DAudio, not the curve itself, so in-place modification is safe
    // here as long as we're not mid-F3DAudioCalculate. The listener
    // constructor seeds at rolloff=1.0; setRolloffFactor re-seeds on
    // change.
    void recompute_volume_curve(float rolloff)
    {
        if (rolloff <= 0.0f) rolloff = 1.0f;  // safety
        for (std::size_t i = 0; i < kVolumeCurvePointCount; ++i)
        {
            const float d = kVolumeCurveSampleDistances[i];
            kVolumeCurvePoints[i].Distance = d / kAudibleRange;
            if (d <= kVolumeCurveMinDistance)
            {
                kVolumeCurvePoints[i].DSPSetting = 1.0f;
            }
            else
            {
                const float effective_d =
                    kVolumeCurveMinDistance
                  + rolloff * (d - kVolumeCurveMinDistance);
                kVolumeCurvePoints[i].DSPSetting =
                    kVolumeCurveMinDistance / effective_d;
            }
        }
    }

    // Doppler clamp: F3DAudio's raw output can spike on fast camera moves
    // or teleports, producing audible "garble". Clamping to roughly a
    // perfect-fifth-down / octave-up keeps the cue intact without the
    // extreme cases. FMOD doesn't clamp — this is a quality win.
    constexpr float kDopplerMin = 0.5f;
    constexpr float kDopplerMax = 2.0f;

    // Custom LPF curve for distance-based HF rolloff ("air absorption").
    // F3DAudio's default LPFDirect curve is a single linear segment
    // 1.0 -> 0.75 across the full audible range — too mild for the SL
    // distance scale where we want clearly muffled sounds at the edge
    // of a region. Frequency parameter at the source voice is [0, 1]
    // where 1 = full HF passthrough, 0 = full cutoff. Distances below
    // are fractions of CurveDistanceScaler (= 384 m at default tunings).
    F3DAUDIO_DISTANCE_CURVE_POINT kLpfCurvePoints[] = {
        { 0.000f, 1.000f },   //    0 m: full HF
        { 0.039f, 1.000f },   //   15 m: hold passthrough in the close field
        { 0.099f, 0.900f },   //   38 m: barely-perceptible HF tilt
        { 0.195f, 0.800f },   //   75 m: clearly "across the parcel" tone
        { 0.391f, 0.600f },   //  150 m: noticeable air absorption
        { 0.781f, 0.400f },   //  300 m: strong muffling
        { 1.000f, 0.300f },   //  384 m: at silence anyway, but cap the bottom
    };
    F3DAUDIO_DISTANCE_CURVE kLpfCurve = {
        kLpfCurvePoints,
        static_cast<uint32_t>(sizeof(kLpfCurvePoints) /
                              sizeof(kLpfCurvePoints[0]))
    };

    // Stereo emitter spread (metres). Channels are positioned at the
    // emitter's left/right by this distance. ~0.5 m is the X3DAudio
    // sample-code default and feels right for the SL "stereo speaker on
    // an object" use case — wide enough to perceive a stereo image when
    // close, narrow enough not to dominate over the emitter's position
    // when far away.
    constexpr float kStereoChannelRadius = 0.5f;
}

LLListener_FAudio::LLListener_FAudio(LLAudioEngine_FAudio* engine)
    : mEnginep(engine)
{
    init();
    // Seed the shared volume curve at the default rolloff. setRolloffFactor
    // regenerates it on change; we need a valid initial state here so the
    // first run_f3d after construction reads a populated curve.
    recompute_volume_curve(mRolloffFactor);
    syncListenerPose();
}

LLListener_FAudio::~LLListener_FAudio() = default;

void LLListener_FAudio::translate(LLVector3 offset)
{
    LLListener::translate(offset);
    syncListenerPose();
}

void LLListener_FAudio::setPosition(LLVector3 pos)
{
    LLListener::setPosition(pos);
    syncListenerPose();
}

void LLListener_FAudio::setVelocity(LLVector3 vel)
{
    LLListener::setVelocity(vel);
    syncListenerPose();
}

void LLListener_FAudio::orient(LLVector3 up, LLVector3 at)
{
    LLListener::orient(up, at);
    syncListenerPose();
}

void LLListener_FAudio::commitDeferredChanges()
{
    // FAudio applies state immediately when SetOutputMatrix is called per
    // channel in apply3D(); there's no separate listener commit needed.
    syncListenerPose();
}

void LLListener_FAudio::setDopplerFactor(F32 factor)
{
    mDopplerFactor = factor;
}

F32 LLListener_FAudio::getDopplerFactor()
{
    return mDopplerFactor;
}

void LLListener_FAudio::setRolloffFactor(F32 factor)
{
    if (mRolloffFactor == factor) return;
    mRolloffFactor = factor;
    // Match FMOD's set3DRolloff behaviour: the rolloff factor changes
    // the *shape* of the inverse-rolloff curve (volume = min_dist /
    // (min_dist + rolloff * (d - min_dist))), not the audible range.
    // Regenerate the shared curve table so the next F3DAudioCalculate
    // pass reads the new gains.
    recompute_volume_curve(factor);
}

F32 LLListener_FAudio::getRolloffFactor()
{
    return mRolloffFactor;
}

void LLListener_FAudio::syncListenerPose()
{
    mF3DListener.OrientFront = linden_to_x3d_unit(mListenAt);
    mF3DListener.OrientTop   = linden_to_x3d_unit(mListenUp);
    mF3DListener.Position    = linden_to_x3d(mPosition);
    mF3DListener.Velocity    = linden_to_x3d(mVelocity);
    mF3DListener.pCone       = nullptr;
}

namespace
{
    // Build an orthonormal emitter basis whose Front points from the
    // emitter toward the listener. Top is world-up Gram-Schmidt'd
    // against Front so the L/R stereo channels stay horizontal in the
    // world even when the listener is off-elevation. Falls back to the
    // canonical fixed orientation for the two degenerate cases:
    // listener-co-located and listener-directly-above/below.
    inline void compute_facing_basis(const F3DAUDIO_VECTOR& emitter_pos,
                                     const F3DAUDIO_VECTOR& listener_pos,
                                     F3DAUDIO_VECTOR& out_front,
                                     F3DAUDIO_VECTOR& out_top)
    {
        float dx = listener_pos.x - emitter_pos.x;
        float dy = listener_pos.y - emitter_pos.y;
        float dz = listener_pos.z - emitter_pos.z;
        float len2 = dx*dx + dy*dy + dz*dz;
        if (len2 < 1e-8f)
        {
            out_front = F3DAUDIO_VECTOR{ 0.f, 0.f, 1.f };
            out_top   = F3DAUDIO_VECTOR{ 0.f, 1.f, 0.f };
            return;
        }
        const float inv_len = 1.0f / std::sqrt(len2);
        const F3DAUDIO_VECTOR front{ dx*inv_len, dy*inv_len, dz*inv_len };

        // Project world-up (X3D +Y) onto the plane perpendicular to front.
        const float dot_uf = front.y;  // world_up = (0,1,0) so dot = front.y
        F3DAUDIO_VECTOR top{ -front.x * dot_uf,
                             1.0f - front.y * dot_uf,
                             -front.z * dot_uf };
        const float top_len2 = top.x*top.x + top.y*top.y + top.z*top.z;
        if (top_len2 < 1e-8f)
        {
            // Front parallel to world-up — listener directly above/below.
            out_front = F3DAUDIO_VECTOR{ 0.f, 0.f, 1.f };
            out_top   = F3DAUDIO_VECTOR{ 0.f, 1.f, 0.f };
            return;
        }
        const float inv_top = 1.0f / std::sqrt(top_len2);
        out_front = front;
        out_top   = F3DAUDIO_VECTOR{ top.x * inv_top, top.y * inv_top, top.z * inv_top };
    }

    // Common F3DAudio dispatch shared between the spatial-3D path and the
    // forced-priority (UI / preview) path. The two differ only in emitter
    // position/velocity and whether Doppler is calculated.
    void run_f3d(LLAudioEngine_FAudio* engine,
                 const F3DAUDIO_LISTENER& listener,
                 LLAudioChannelFAudio* channel,
                 const F3DAUDIO_VECTOR& emitter_pos_x3d,
                 const F3DAUDIO_VECTOR& emitter_vel_x3d,
                 float rolloff,
                 float doppler_scaler,
                 bool calc_doppler)
    {
        FAudioSourceVoice* voice = channel->getVoice();
        if (!voice) return;

        const uint32_t src_channels = channel->getSourceChannelCount();
        const uint32_t dst_channels = engine->getOutputChannelCount();
        if (src_channels == 0 || dst_channels == 0) return;

        // Multi-channel source voices require emitter.ChannelCount to
        // match SrcChannelCount with pChannelAzimuths set, otherwise the
        // mono path writes mat[speaker * 1 + 0] while the real matrix
        // layout is mat[speaker * SrcChannelCount + src] — the rest of
        // the entries stay zero and src channels > 0 are silenced.
        //
        // For stereo (the common case) the emitter's basis is rebuilt
        // each frame so OrientFront points from emitter toward listener
        // — the emitter "faces the listener". With that frame, the L/R
        // image is consistent regardless of where the listener has
        // moved (vs the alternative fixed-orientation model where
        // walking around the back of an emitter flips L/R).
        //
        // Azimuth mapping: emitter's right (azimuth π/2) ends up on the
        // listener's left because emitter and listener face each other
        // (mirror). So LEFT audio channel goes at π/2 and RIGHT at 3π/2.
        std::array<float, 8> azimuths{};  // zero-initialised; > 2 chans
                                          // co-locate (rare in SL).
        if (src_channels == 2)
        {
            azimuths[0] = F3DAUDIO_PI * 0.5f;   // L audio  -> emitter right
            azimuths[1] = F3DAUDIO_PI * 1.5f;   // R audio  -> emitter left
        }
        F3DAUDIO_EMITTER emitter{};
        emitter.Position = emitter_pos_x3d;
        emitter.Velocity = emitter_vel_x3d;
        if (src_channels == 2)
        {
            compute_facing_basis(emitter_pos_x3d, listener.Position,
                                 emitter.OrientFront, emitter.OrientTop);
        }
        else
        {
            emitter.OrientFront = F3DAUDIO_VECTOR{ 0.f, 0.f, 1.f };
            emitter.OrientTop   = F3DAUDIO_VECTOR{ 0.f, 1.f, 0.f };
        }
        emitter.ChannelCount = src_channels;
        emitter.ChannelRadius = (src_channels > 1) ? kStereoChannelRadius : 0.f;
        emitter.pChannelAzimuths = (src_channels > 1) ? azimuths.data() : nullptr;
        emitter.InnerRadius = engine->getInnerRadius();
        emitter.InnerRadiusAngle = F3DAUDIO_PI / 4.0f;
        // CurveDistanceScaler stays fixed at the audible range. Rolloff
        // is baked into the shared volume curve by recompute_volume_curve
        // — see the kVolumeCurvePoints comment — matching FMOD's
        // set3DRolloff behaviour where the factor changes curve shape
        // rather than reach. rolloff parameter is unused here but kept
        // in the signature for symmetry with the listener interface.
        (void)rolloff;
        emitter.CurveDistanceScaler = kAudibleRange;
        emitter.DopplerScaler = doppler_scaler;
        emitter.pVolumeCurve = &kVolumeCurve;
        emitter.pLFECurve = nullptr;
        // Drive the source voice's LPF cutoff from a curve tuned for the
        // SL audible range rather than F3DAudio's mild default. Reverb
        // path stays on the default — wet signal is already an ambient
        // wash and per-tap LPF on top would muddy the global reverb
        // submix more than help spatially.
        emitter.pLPFDirectCurve = &kLpfCurve;
        emitter.pLPFReverbCurve = nullptr;
        emitter.pReverbCurve = nullptr;
        emitter.pCone = nullptr;

        std::array<float, 64> matrix{};
        F3DAUDIO_DSP_SETTINGS dsp{};
        dsp.SrcChannelCount = src_channels;
        dsp.DstChannelCount = dst_channels;
        dsp.pMatrixCoefficients = matrix.data();
        dsp.DopplerFactor = 1.f;

        // CALCULATE_LPF_DIRECT fills dsp.LPFDirectCoefficient — a [0, 1]
        // factor representing high-frequency passthrough vs cutoff,
        // derived from distance (default curve) or pLPFDirectCurve. Maps
        // directly onto FAudio's voice filter Frequency parameter so
        // distant sources get an air-absorption HF rolloff for free.
        // CALCULATE_REVERB fills dsp.ReverbLevel — the per-emitter wet
        // send level fed into the engine's reverb submix.
        uint32_t flags = F3DAUDIO_CALCULATE_MATRIX
                       | F3DAUDIO_CALCULATE_LPF_DIRECT
                       | F3DAUDIO_CALCULATE_REVERB;
        if (calc_doppler) flags |= F3DAUDIO_CALCULATE_DOPPLER;
        F3DAudioCalculate(engine->getX3DInstance(), &listener, &emitter,
                          flags, &dsp);

        FAudioVoice* dest = channel->getDestVoice();
        if (!dest) dest = reinterpret_cast<FAudioVoice*>(engine->getMasterVoice());
        FAudioVoice_SetOutputMatrix(voice, dest,
                                    dsp.SrcChannelCount, dsp.DstChannelCount,
                                    dsp.pMatrixCoefficients, FAUDIO_COMMIT_NOW);

        // Air-absorption / occlusion LPF. F3DAudio's coefficient maps
        // directly onto FAudio's normalized [0, 1] Frequency param —
        // 1 = full passthrough, lower values cut HF progressively.
        //
        // FAudio does not interpolate SetFilterParameters across an audio
        // frame the way it does SetVolume / SetOutputMatrix — the new
        // biquad coefficients are applied immediately while y[n-1]/y[n-2]
        // still reflect the old filter response. A meaningful per-frame
        // step in the cutoff (camera move, teleport, fast forward motion)
        // therefore produces an IIR transient audible as a "pop". Smooth
        // the coefficient on the main-thread side and skip the call when
        // the smoothed value has settled at passthrough so a freshly-
        // built voice (whose filter defaults to Frequency=1.0) stays
        // bit-identical to the source until distance starts attenuating.
        //
        // Voices created without FAUDIO_VOICE_USEFILTER (forced-priority
        // sources at listener distance 0) skip this entirely —
        // SetFilterParameters is invalid on those voices.
        if (channel->getVoiceUsesFilter())
        {
            constexpr float kLpfSmoothAlpha       = 0.18f;
            constexpr float kLpfPassthroughThresh = 0.999f;
            constexpr float kLpfApplyEpsilon      = 0.002f;
            const float target_lpf = dsp.LPFDirectCoefficient;
            channel->mSmoothedLpf +=
                (target_lpf - channel->mSmoothedLpf) * kLpfSmoothAlpha;
            const bool at_passthrough =
                channel->mSmoothedLpf    >= kLpfPassthroughThresh &&
                channel->mLastAppliedLpf >= kLpfPassthroughThresh;
            if (!at_passthrough &&
                std::abs(channel->mSmoothedLpf - channel->mLastAppliedLpf)
                    > kLpfApplyEpsilon)
            {
                FAudioFilterParameters lpf{};
                lpf.Type = FAudioLowPassFilter;
                lpf.Frequency = channel->mSmoothedLpf;
                lpf.OneOverQ  = 1.0f;
                FAudioVoice_SetFilterParameters(voice, &lpf, FAUDIO_COMMIT_NOW);
                channel->mLastAppliedLpf = channel->mSmoothedLpf;
            }
        }

        // Reverb wet send. Spatial sounds bleed into the reverb submix at
        // dsp.ReverbLevel (a single scalar from F3DAudio); each source
        // channel routes equally into every reverb input channel so the
        // reverb is heard as ambient wash rather than spatially placed.
        // Forced-priority (UI) sounds get a zero send so they stay dry.
        //
        // The reverb send bypasses the per-type submix that carries the
        // secondary (per-type slider) gain — so for channels routed
        // through a group submix we have to fold that gain into the
        // reverb matrix manually, otherwise lowering e.g. the SFX
        // slider muting the dry signal leaves the reverb at full level.
        // Channels that fell back to direct master routing already have
        // secondary multiplied into voice volume, which gates the reverb
        // send naturally.
        FAudioSubmixVoice* reverb = engine->getReverbVoice();
        if (reverb)
        {
            const float secondary_for_reverb = channel->isRoutedThroughGroup()
                ? channel->getSecondaryGain()
                : 1.0f;
            const float target_send = calc_doppler
                ? (dsp.ReverbLevel * engine->getReverbSendScale() * secondary_for_reverb)
                : 0.0f;

            // Smooth + gate the reverb send the same way we treat the
            // LPF coefficient. FAudio interpolates SetOutputMatrix across
            // the audio frame, but per-frame writes still produce small
            // steps when the underlying scalar (F3DAudio's dsp.ReverbLevel
            // composed with the engine's send scale) jumps rapidly during
            // fast listener motion. Smoothing absorbs those jumps and the
            // gate skips the matrix write entirely once a dry channel has
            // settled at silence — forced-priority sources (calc_doppler
            // == false → target_send always 0) stay there permanently.
            constexpr float kReverbSmoothAlpha    = 0.20f;
            constexpr float kReverbSilenceThresh  = 0.0001f;
            constexpr float kReverbApplyEpsilon   = 0.001f;
            channel->mSmoothedReverbSend +=
                (target_send - channel->mSmoothedReverbSend) * kReverbSmoothAlpha;
            const bool at_silence =
                channel->mSmoothedReverbSend    <= kReverbSilenceThresh &&
                channel->mLastAppliedReverbSend <= kReverbSilenceThresh;
            if (!at_silence &&
                std::abs(channel->mSmoothedReverbSend
                         - channel->mLastAppliedReverbSend) > kReverbApplyEpsilon)
            {
                // Matrix dims target the reverb submix's input channel
                // count, not the master's. The reverb submix may be a
                // different size (e.g. 5.1 reverb feeding a 7.1 master);
                // FAudio's default submix->master matrix handles the
                // channel-count conversion on the output side.
                const uint32_t reverb_ch = engine->getReverbChannelCount();
                std::array<float, 64> reverb_mat{};
                const float send = channel->mSmoothedReverbSend;
                for (uint32_t s = 0; s < src_channels && s < 8; ++s)
                {
                    for (uint32_t d = 0; d < reverb_ch && d < 8; ++d)
                    {
                        reverb_mat[d * src_channels + s] = send;
                    }
                }
                FAudioVoice_SetOutputMatrix(voice,
                                            reinterpret_cast<FAudioVoice*>(reverb),
                                            src_channels, reverb_ch,
                                            reverb_mat.data(), FAUDIO_COMMIT_NOW);
                channel->mLastAppliedReverbSend = channel->mSmoothedReverbSend;
            }
        }

        float target = calc_doppler ? dsp.DopplerFactor : 1.0f;
        if (target < kDopplerMin) target = kDopplerMin;
        else if (target > kDopplerMax) target = kDopplerMax;
        // Exponential smoothing on the per-channel doppler factor.
        // Alpha ~0.2 reaches 50% in ~3 frames / ~50 ms at 60 Hz —
        // absorbs single-frame velocity spikes from teleports or flycam
        // without making the cue feel laggy. Smoothing runs in log
        // space so a slowdown (1.0 -> 0.5, a downward octave) and a
        // speedup (1.0 -> 2.0, an upward octave) take the same number
        // of frames to settle in pitch perception — linear smoothing
        // is biased toward asymmetric ramps because frequency ratio is
        // multiplicative, not additive.
        constexpr float kSmoothAlpha = 0.2f;
        const float log_target = std::log(std::max(kDopplerMin, target));
        const float log_curr   = std::log(std::max(kDopplerMin,
                                                   channel->mSmoothedDoppler));
        const float log_next   = log_curr + (log_target - log_curr) * kSmoothAlpha;
        channel->mSmoothedDoppler = std::exp(log_next);
        FAudioSourceVoice_SetFrequencyRatio(voice, channel->mSmoothedDoppler,
                                            FAUDIO_COMMIT_NOW);
    }
}

void LLListener_FAudio::apply3D(LLAudioChannelFAudio* channel)
{
    LL_PROFILE_ZONE_SCOPED;
    if (!channel || !mEnginep) return;
    LLAudioSource* source = channel->getSource();
    if (!source) return;

    LLVector3 pos;
    pos.setVec(source->getPositionGlobal());
    run_f3d(mEnginep, mF3DListener, channel,
            linden_to_x3d(pos),
            linden_to_x3d(source->getVelocity()),
            mRolloffFactor, mDopplerFactor, /*calc_doppler=*/true);
}

void LLListener_FAudio::applyForcedPriority(LLAudioChannelFAudio* channel)
{
    LL_PROFILE_ZONE_SCOPED;
    if (!channel || !mEnginep) return;

    // Emitter co-located with the listener so distance == 0; F3DAudio's
    // near-field diffusion (driven by InnerRadius) spreads energy equally
    // across all output speakers. Velocity zero, Doppler disabled.
    run_f3d(mEnginep, mF3DListener, channel,
            mF3DListener.Position,
            F3DAUDIO_VECTOR{ 0.f, 0.f, 0.f },
            mRolloffFactor, 0.f, /*calc_doppler=*/false);
}
