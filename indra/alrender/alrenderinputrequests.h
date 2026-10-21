/**
 * @file alrenderinputrequests.h
 * @brief What the render inputs' changes since they were last applied ask of the next application, and what it does.
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

#include "alrenderresourceinputs.h"
#include "alshaderinputs.h"

#include "stdtypes.h"

#include <array>
#include <optional>

/// The changes to the render inputs since they were last applied (LLPipeline::applyRenderInputs). Settings' listeners
/// and the window mark them as they happen and do nothing else; the frame applies them all at once, so a graphics
/// preset, a Look or a graphics level that changes a dozen settings latches the inputs, reloads the programs and
/// reconciles the resources once.
///
/// A snapshot holds the render size while it lasts (ALRenderSizeOverride): no application runs until the last one
/// ends, and its end marks the change back, so snapshots taken back to back at one size, a 360 capture's six faces,
/// reconcile the resources to the snapshot's size once and back once.
class ALRenderInputRequests
{
public:
    enum class Change : U8
    {
        Inputs,   ///< a setting the programs or the render resources are built from: the inputs are latched again, and
                  ///< what moved applied
        Window,   ///< the window's size: the frame that applies it is not drawn
        Programs, ///< every program built again from its sources, whatever the inputs say: a purged binary cache
    };

    void mark(Change change);

    /// A snapshot holds the render size from now until overrideEnds, and no application runs meanwhile.
    void overrideBegins();
    /// It ends: the next application latches the inputs again, the size among them.
    void overrideEnds();
    /// Whether a snapshot holds the render size.
    bool overriding() const { return mOverrides > 0; }

    /// Whether an application is waiting to run: a change is marked and no snapshot holds the size.
    bool pending() const;
    /// Whether the window has been resized since the last application, which waits for the snapshot holding the
    /// size, if one does.
    bool windowResized() const;

    /// What one application of the waiting changes asks for.
    struct Application
    {
        bool mRebuildPrograms = false; ///< every program from its sources, whatever the inputs say
        bool mSkipFrame       = false; ///< draw nothing this frame, the window having been resized
        U32  mChanges         = 0;     ///< the changes it applies
    };

    /// The waiting changes' application, which then wait no longer: a change marked while it runs is the next
    /// application's. Nothing while no change is marked or a snapshot holds the size.
    std::optional<Application> take();

private:
    std::array<U32, 3> mMarks{};
    U32                mOverrides = 0;
};

/// What one application does with the inputs it latched, in this order: the programs loaded for the shader inputs,
/// the resources reconciled to theirs, and then what derives from either invalidated. A pure function of what was asked
/// for and what the inputs were and are, so that the frame's application, the one path into a reload or a reconcile,
/// does each only where it has something to do.
struct ALRenderApplication
{
    enum class Programs : U8
    {
        Kept,     ///< the programs are loaded for the inputs latched: none is touched
        Reloaded, ///< an input moved: every loader configures its programs, and only those it configured differently
                  ///< link again
        Rebuilt,  ///< every program built from its sources: none is loaded (the first load, a new context), or it was
                  ///< asked for
    };

    Programs mPrograms = Programs::Kept;
    /// The shared objects compiled again, which every program attaching them links again: the programs are rebuilt,
    /// or a global shader input moved (ALShaderInputs::Global).
    bool mSharedObjects = false;
    /// The resources reconciled: none are built yet, or their inputs moved from those the last reconcile was asked for.
    bool mReconcile = false;

    /// @a loaded: the shader inputs the programs were loaded for, if any are. @a reconciled: the inputs the last
    /// reconcile was asked for, if the resources are built.
    static ALRenderApplication make(bool rebuild_programs, const std::optional<ALShaderInputs>& loaded,
                                    const ALShaderInputs& latched, const std::optional<ALRenderResourceInputs>& reconciled,
                                    const ALRenderResourceInputs& requested);

    /// Whether it does anything at all.
    bool idle() const { return mPrograms == Programs::Kept && !mReconcile; }
};
