/**
 * @file llhbgpu.h
 * @brief Single source of truth for HarfBuzz-GPU (hb-gpu) availability.
 *
 * libharfbuzz-gpu is linked unconditionally (see indra/cmake/Harfbuzz.cmake).
 * This header keys the C++ capability macro off the HarfBuzz version rather
 * than a propagated -D, so it cannot desync the per-target define set against
 * the shared precompiled header (which would break PCH reuse). Include this
 * wherever hb-gpu is used and guard with `#if LL_HAS_HB_GPU`.
 *
 * 14.2.0 is the gate: it introduced the paint encoder and the stabilized draw
 * API, and is the version the project vendors.
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

#ifndef LL_LLHBGPU_H
#define LL_LLHBGPU_H

#include <hb.h>

#if HB_VERSION_ATLEAST(14, 2, 0)
#  define LL_HAS_HB_GPU 1
#  include <hb-gpu.h>
#else
#  define LL_HAS_HB_GPU 0
#endif

#endif // LL_LLHBGPU_H
