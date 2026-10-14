/**
 * @file newview_test_checks.cpp
 * @brief What the viewer's tests share a process with: made before the first
 *        case, and watched after each.
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

#include "linden_common.h"

#include "../alrecovery.h"
#include "../alscriptstudioviewer.h"

#include "almainloop.h"
#include "alsavehistory.h"
#include "llcontrol.h"
#include "llerrorcontrol.h"
#include "threadpool.h"
#include "workqueue.h"

#include "alteststateguard.h"
#include "newview_test_fakes.h"

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    // A pointer as text: which object, or none. Only ever printed, never
    // followed, since what it points at may be gone.
    std::string which(const void* p)
    {
        return p ? llformat("%p", p) : std::string("none");
    }

    // The names a tracked class's live instances go by, sorted.
    template <typename TRACKED>
    std::string liveNames()
    {
        std::vector<std::string> names;
        for (const std::string& name : typename TRACKED::key_snapshot())
        {
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        std::string text;
        for (const std::string& name : names)
        {
            text += (text.empty() ? "" : ", ") + name;
        }
        return text;
    }

    class NewviewChecks : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(Catch::TestRunInfo const&) override
        {
            // The queue a case chooses to stand in for the main loop's, made
            // before the first case and kept for the run: work handed back to
            // it for a case gone finds it still there. Under a name of its
            // own, so being made chooses nothing.
            newview_test::mainLoop();

            // llcommon's checks, without its event pumps made afresh for each
            // case: the viewer's own event APIs, LLViewerControlListener's
            // among them, listen on the pumps from before main, as they do for
            // the viewer's whole run. A case that starts worker threads stops
            // them before it ends; one that turns logging up or down puts it
            // back.
            ll_test::StateGuard::add("the live thread pools", liveNames<LL::ThreadPoolBase>);
            ll_test::StateGuard::add("the live work queues", liveNames<LL::WorkQueueBase>);
            ll_test::StateGuard::add("LLError's default level",
                                     [] { return std::to_string(LLError::getDefaultLevel()); });
            // A case that chooses a main loop lets go of the choice: every
            // case after it would hand its work back to it.
            ll_test::StateGuard::add("the main loop's queue", [] { return which(ALMainLoop::queue().get()); });
            // A case that makes a settings group of its own lets it go: one
            // left would answer for that name in every case after it.
            ll_test::StateGuard::add("the settings groups", liveNames<LLControlGroup>);
            // Script Studio's recovery, its save history and its viewer are
            // each one for the process: a case that uses one of its own puts
            // the one before back.
            ll_test::StateGuard::add("the recovery store", [] { return which(ALRecovery::store()); });
            ll_test::StateGuard::add("the save history", [] { return which(ALRecovery::history().get()); });
            ll_test::StateGuard::add("Script Studio's viewer", [] { return which(&ALScriptStudioViewer::get()); });
        }
    };
}

CATCH_REGISTER_LISTENER(NewviewChecks)
