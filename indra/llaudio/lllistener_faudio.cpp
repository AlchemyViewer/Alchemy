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

    // Custom distance-attenuation curve. Distances are normalized to
    // CurveDistanceScaler (which we set to BASE_AUDIBLE_RANGE / rolloff
    // below). Points approximate 1/d inverse rolloff through the audible
    // mid-range so it sits in the same ballpark as FMOD's default
    // INVERSEROLLOFF, but with two quality wins:
    //   1. A held near-field (full volume to 1% of scaler distance) — close
    //      sounds don't get over-attenuated by sub-metre position jitter.
    //   2. Hits actual silence at the scaler distance instead of FMOD's
    //      asymptotic 0.01 tail — cleaner mix, no ghost sounds at extreme
    //      distance.
    // Distances below are fractions of CurveDistanceScaler (= 384 m at
    // default rolloff), e.g. 0.039 -> 15 m. Near and mid-range gains track
    // 1/d at the same absolute metres as before; the curve just extends
    // its tail further so ambients placed at one corner of a 256x256 SL
    // region don't hard-cut from the opposite corner (diagonal ~362 m).
    F3DAUDIO_DISTANCE_CURVE_POINT kVolumeCurvePoints[] = {
        { 0.000f, 1.000f },   //    0 m: full
        { 0.004f, 1.000f },   //  1.5 m: hold near-field
        { 0.008f, 0.500f },   //    3 m: 1/d
        { 0.020f, 0.200f },   //  7.5 m: 1/d
        { 0.039f, 0.100f },   //   15 m: 1/d
        { 0.099f, 0.040f },   //   38 m: 1/d
        { 0.195f, 0.020f },   //   75 m: 1/d
        { 0.391f, 0.005f },   //  150 m: long-tail (~-46 dB)
        { 1.000f, 0.000f },   //  384 m: clean silence
    };
    F3DAUDIO_DISTANCE_CURVE kVolumeCurve = {
        kVolumeCurvePoints,
        static_cast<uint32_t>(sizeof(kVolumeCurvePoints) /
                              sizeof(kVolumeCurvePoints[0]))
    };

    // Default audible range in metres before the curve hits silence. SL
    // regions are 256 x 256 m, so the corner-to-corner diagonal is ~362
    // m; 384 m gives that plus a small margin without bleeding sounds in
    // from adjacent regions. The listener's rolloff factor compresses
    // this: rolloff=1 -> 384 m, rolloff=2 -> 192 m, rolloff=0.5 -> 768 m.
    constexpr float kBaseAudibleRange = 384.0f;

    // Doppler clamp: F3DAudio's raw output can spike on fast camera moves
    // or teleports, producing audible "garble". Clamping to roughly a
    // perfect-fifth-down / octave-up keeps the cue intact without the
    // extreme cases. FMOD doesn't clamp — this is a quality win.
    constexpr float kDopplerMin = 0.5f;
    constexpr float kDopplerMax = 2.0f;

    // Stereo emitter spread (metres). Channels are positioned at the
    // emitter's left/right by this distance. ~0.5 m is the X3DAudio
    // sample-code default and feels right for the SL "stereo speaker on
    // an object" use case — wide enough to perceive a stereo image when
    // close, narrow enough not to dominate over the emitter's position
    // when far away.
    constexpr float kStereoChannelRadius = 0.5f;

    // Global scale applied to dsp.ReverbLevel before it hits the per-
    // source -> reverb-submix send. Aim is "barely perceptible space"
    // rather than an obvious effect — at close range with default
    // ReverbLevel ~1.0 this yields a -20 dB send into the reverb, well
    // below the dry signal. Far sources get even less because
    // ReverbLevel itself decreases with distance.
    constexpr float kReverbSendScale = 0.05f;
}

LLListener_FAudio::LLListener_FAudio(LLAudioEngine_FAudio* engine)
    : mEnginep(engine)
{
    init();
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
    mRolloffFactor = factor;
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
        // For stereo (the common case) we additionally spread the
        // channels: ChannelRadius positions them on the emitter's
        // left/right, giving a real stereo image. The azimuth-to-listener
        // mapping looks swapped because the emitter "faces forward"
        // (OrientFront = +Z) and the listener looking back at the emitter
        // sees a mirror — the emitter's right side ends up on the
        // listener's left, so the LEFT audio channel goes at azimuth π/2
        // (emitter's right) and RIGHT at 3π/2 (emitter's left).
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
        emitter.OrientFront = F3DAUDIO_VECTOR{ 0.f, 0.f, 1.f };
        emitter.OrientTop   = F3DAUDIO_VECTOR{ 0.f, 1.f, 0.f };
        emitter.ChannelCount = src_channels;
        emitter.ChannelRadius = (src_channels > 1) ? kStereoChannelRadius : 0.f;
        emitter.pChannelAzimuths = (src_channels > 1) ? azimuths.data() : nullptr;
        emitter.InnerRadius = DEFAULT_MIN_DISTANCE;
        emitter.InnerRadiusAngle = F3DAUDIO_PI / 4.0f;
        emitter.CurveDistanceScaler = (rolloff > 0.f)
            ? (kBaseAudibleRange / rolloff)
            : kBaseAudibleRange;
        emitter.DopplerScaler = doppler_scaler;
        emitter.pVolumeCurve = &kVolumeCurve;
        emitter.pLFECurve = nullptr;
        emitter.pLPFDirectCurve = nullptr;
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
        FAudioFilterParameters lpf{};
        lpf.Type = FAudioLowPassFilter;
        lpf.Frequency = dsp.LPFDirectCoefficient;
        lpf.OneOverQ = 1.0f;
        FAudioVoice_SetFilterParameters(voice, &lpf, FAUDIO_COMMIT_NOW);

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
            const float send_gain = calc_doppler
                ? (dsp.ReverbLevel * kReverbSendScale * secondary_for_reverb)
                : 0.0f;
            const uint32_t reverb_ch = engine->getOutputChannelCount();
            std::array<float, 64> reverb_mat{};
            for (uint32_t s = 0; s < src_channels && s < 8; ++s)
            {
                for (uint32_t d = 0; d < reverb_ch && d < 8; ++d)
                {
                    reverb_mat[d * src_channels + s] = send_gain;
                }
            }
            FAudioVoice_SetOutputMatrix(voice,
                                        reinterpret_cast<FAudioVoice*>(reverb),
                                        src_channels, reverb_ch,
                                        reverb_mat.data(), FAUDIO_COMMIT_NOW);
        }

        float target = calc_doppler ? dsp.DopplerFactor : 1.0f;
        if (target < kDopplerMin) target = kDopplerMin;
        else if (target > kDopplerMax) target = kDopplerMax;
        // Exponential smoothing on the per-channel doppler factor. Alpha
        // ~0.2 reaches 50% in ~3 frames / ~50 ms at 60 Hz; absorbs
        // single-frame velocity spikes from teleports or flycam without
        // making the cue feel laggy.
        constexpr float kSmoothAlpha = 0.2f;
        channel->mSmoothedDoppler +=
            (target - channel->mSmoothedDoppler) * kSmoothAlpha;
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
    if (!channel || !mEnginep) return;

    // Emitter co-located with the listener so distance == 0; F3DAudio's
    // near-field diffusion (driven by InnerRadius) spreads energy equally
    // across all output speakers. Velocity zero, Doppler disabled.
    run_f3d(mEnginep, mF3DListener, channel,
            mF3DListener.Position,
            F3DAUDIO_VECTOR{ 0.f, 0.f, 0.f },
            mRolloffFactor, 0.f, /*calc_doppler=*/false);
}
