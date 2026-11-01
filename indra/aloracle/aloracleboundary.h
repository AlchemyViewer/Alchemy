/**
 * @file aloracleboundary.h
 * @brief What every aloracle function does at the C boundary.
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

#include "aloracle.h"

#include "llmutex.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

class LLSD;

namespace aloracle
{
    /// What an LL_ERRS throws inside the library, once prepare() has run.
    struct FatalError
    {
    };

    /// Makes every LL_ERRS in the library throw FatalError. Runs once, before
    /// the first call's body; every later call waits for it.
    void prepare();

    /// What every call's body runs under. LLError takes its own lock with
    /// try_to_lock, and an LL_ERRS that finds another thread logging skips
    /// the fatal function and crashes, or, at a site met for the first time,
    /// is skipped and its caller goes on: no two calls log at once.
    LLMutex& callLock();

    /// Runs a call's body, which returns a status, under callLock(), and
    /// turns whatever it throws into one.
    template <typename Body>
    int32_t guarded(Body&& body) noexcept
    {
        try
        {
            LLMutexLock lock(&callLock());
            prepare();
            return body();
        }
        catch (const FatalError&)
        {
            return ALORACLE_FATAL;
        }
        catch (...)
        {
            return ALORACLE_EXCEPTION;
        }
    }

    /// Empties *out for a call to fill. False where out is null.
    bool clear(aloracle_bytes* out);

    /// The bytes passed in as text. False where data is null and size is not.
    bool take(const uint8_t* data, size_t size, std::string& text);

    /// The LLSD binary passed in, read whole. False where it is not one value
    /// that fills the bytes.
    bool takeValue(const uint8_t* data, size_t size, LLSD& value);

    /// Hands text to the caller in *out, cleared already.
    int32_t give(std::string_view text, aloracle_bytes* out);

    /// Hands a value to the caller in *out, in LLSD binary.
    int32_t giveValue(const LLSD& value, aloracle_bytes* out);
}
