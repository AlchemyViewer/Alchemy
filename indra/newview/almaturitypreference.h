/**
 * @file almaturitypreference.h
 * @brief ALMaturityPreference: the agent's maturity preference as asked of the server and as it answered
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

// Each change of the maturity preference is asked of the server with the region's UpdateAgentInformation, and each
// answer counted; a teleport waits while any change asked is unanswered. Only a change that was asked is counted,
// since no answer comes to one that wasn't, as when there is no region or the region grants no such capability: such
// a change fails at once. Once every change asked has its answer, the server's maturity either matches the last one
// asked, or the last is asked again, up to MAX_RETRIES times, before the change fails.
class ALMaturityPreference
{
public:
    static constexpr U32 MAX_RETRIES = 3;

    enum class Outcome : U8
    {
        WAITING, // changes asked are unanswered
        SYNCED,  // the server has the maturity last asked for
        RETRY,   // the server answered with another: ask for the last one again
        FAILED,  // the change could not be made
    };

    // The maturity the agent has with the server to begin with.
    explicit ALMaturityPreference(U8 maturity = 0) : mRequested(maturity), mAnswered(maturity) {}

    // The preference changed to maturity, or a retry asks for it again: ask() asks the server and says whether it
    // could. The change is counted while ask() runs, since an answer can come before it returns, and not counted
    // if it couldn't ask.
    template <class Ask>
    Outcome request(U8 maturity, Ask&& ask)
    {
        mRequested = maturity;
        ++mAsked;
        if (ask())
        {
            return Outcome::WAITING;
        }
        --mAsked;
        mRetries = 0;
        return Outcome::FAILED;
    }

    // The server answered a change with the maturity it now has.
    Outcome answered(U8 maturity)
    {
        ++mAnswers;
        mAnswered = maturity;
        if (!synced())
        {
            return Outcome::WAITING;
        }
        if (mRequested == mAnswered)
        {
            mRetries = 0;
            return Outcome::SYNCED;
        }
        if (++mRetries <= MAX_RETRIES)
        {
            return Outcome::RETRY;
        }
        mRetries = 0;
        return Outcome::FAILED;
    }

    // A change asked failed, with no answer of a maturity.
    Outcome failed()
    {
        ++mAnswers;
        if (!synced())
        {
            return Outcome::WAITING;
        }
        mRetries = 0;
        return mRequested == mAnswered ? Outcome::SYNCED : Outcome::FAILED;
    }

    // Whether every change asked has been answered.
    bool synced() const { return mAsked == mAnswers; }

    // The maturity last asked for, and the maturity the server last answered with.
    U8 requestedMaturity() const { return mRequested; }
    U8 answeredMaturity() const { return mAnswered; }
    // How many times the last change has been asked again.
    U32 retries() const { return mRetries; }

private:
    U32 mAsked = 0;
    U32 mAnswers = 0;
    U32 mRetries = 0;
    U8  mRequested;
    U8  mAnswered;
};
