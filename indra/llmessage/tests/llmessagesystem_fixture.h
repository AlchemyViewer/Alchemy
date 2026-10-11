/**
 * @file llmessagesystem_fixture.h
 * @brief A message system a test starts and ends itself.
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

#ifndef LL_LLMESSAGESYSTEM_FIXTURE_H
#define LL_LLMESSAGESYSTEM_FIXTURE_H

#include "message.h"
#include "net.h"

namespace ll_test
{
    // gMessageSystem is a message system of the test's own while this lives,
    // and what it was before once it ends. The system reads no template file,
    // so it is the disconnected one the tests want, and its socket is on a
    // port the OS picks, so two of them never contend for one.
    class MessageSystemScope
    {
    public:
        MessageSystemScope()
            : mPrior(gMessageSystem)
        {
            const F32 circuit_heartbeat_interval = 5;
            const F32 circuit_timeout = 100;
            start_messaging_system("notafile", NET_USE_OS_ASSIGNED_PORT,
                                   1,
                                   0,
                                   0,
                                   false,
                                   "notasharedsecret",
                                   NULL,
                                   false,
                                   circuit_heartbeat_interval,
                                   circuit_timeout);
        }

        ~MessageSystemScope()
        {
            LLMessageSystem* system = gMessageSystem;
            // Without its template the system is not OK, and a system that is
            // not OK leaves its socket open when it is deleted, so the scope
            // closes it. A socket that never opened is still 0.
            if (system && !system->isOK() && system->mSocket > 0)
            {
                end_net(system->mSocket);
            }
            // Not end_messaging_system(): a system that is not OK never
            // started the transfer manager that would end.
            delete system;
            gMessageSystem = mPrior;
        }

        MessageSystemScope(const MessageSystemScope&) = delete;
        MessageSystemScope& operator=(const MessageSystemScope&) = delete;

    private:
        LLMessageSystem* mPrior;
    };
}

#endif // LL_LLMESSAGESYSTEM_FIXTURE_H
