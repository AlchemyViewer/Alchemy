/**
 * @file alrenderinputrequests.h
 * @brief What the render inputs' changes since they were last applied ask of the next application.
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

#include <array>

/// The changes to the render inputs since they were last applied (LLPipeline::applyRenderInputs). Settings' listeners
/// and the window mark them as they happen and do nothing else; the frame applies them all at once, so a graphics
/// preset, a Look or a graphics level that changes a dozen settings reloads the programs and reconciles the resources
/// once.
class ALRenderInputRequests
{
public:
    enum class Change : U8
    {
        Resources, ///< an input of the render resources (ALRenderResourceInputs)
        Shaders,   ///< an input of the programs: all of them reloaded, which reconciles the resources after them
        Window,    ///< the window's size: the frame that applies it is not drawn
    };

    void mark(Change change);

    /// Whether a change is waiting for an application.
    bool pending() const;
    /// Whether the window has been resized since the last application.
    bool windowResized() const;

    /// What one application of the waiting changes does.
    struct Application
    {
        bool mReloadShaders = false; ///< reload every program; the reload reconciles the resources after them
        bool mReconcile     = false; ///< reconcile the resources, with no reload
        bool mSkipFrame     = false; ///< draw nothing this frame, the window having been resized
        U32  mChanges       = 0;     ///< the changes it applies
    };

    /// The application of the waiting changes, which then wait no longer: a change marked while it runs is the next
    /// application's. @a inputs_changed says whether the render resources' inputs differ from those the last reconcile
    /// was asked for; where they do not, a reconcile would build what that one built, and none is asked for.
    Application take(bool inputs_changed);

    /// A reload of the programs begins, outside an application (LLPipeline::init, a restored context, a menu) or inside
    /// one: the shader and resource changes waiting now are the ones it applies.
    void reloadBegins();
    /// The programs were reloaded and the resources reconciled after them: the shader and resource changes marked before
    /// the reload began are applied, and those marked while it ran wait for the next application. A resize still leaves
    /// its frame undrawn.
    void shadersReloaded();

private:
    std::array<U32, 3> mMarks{};
    // The marks waiting when the reload running began.
    std::array<U32, 3> mReloading{};
};
