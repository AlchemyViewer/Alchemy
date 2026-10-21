/**
 * @file llcorehttputil_test.cpp
 * @brief An HTTP coroutine's handler, and the pump its reply is posted to.
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

#include "../llcorehttputil.h"

#include "httpresponse.h"
#include "llcoros.h"
#include "llevents.h"

#include <boost/fiber/operations.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace
{
    class ReplyHandler : public LLCoreHttpUtil::HttpCoroHandler
    {
    public:
        explicit ReplyHandler(LLEventStream& reply) : HttpCoroHandler(reply) {}

    protected:
        LLSD handleSuccess(LLCore::HttpResponse*, LLCore::HttpStatus&) const override
        {
            LLSD result = LLSD::emptyMap();
            result["answer"] = 42;
            return result;
        }

        LLSD parseBody(LLCore::HttpResponse*, bool& success) const override
        {
            success = true;
            return LLSD();
        }
    };

    // A successful reply with no body, as the HTTP library hands it over.
    void deliver(LLCoreHttpUtil::HttpCoroHandler& handler)
    {
        LLCore::HttpResponse* response = new LLCore::HttpResponse();
        response->setStatus(LLCore::HttpStatus());
        handler.onCompleted(LLCORE_HTTP_HANDLE_INVALID, response);
        response->release();
    }

    TEST_CASE("llcorehttputil: a coroutine waiting on its pump gets its reply", "[llcorehttputil]")
    {
        LLEventStream pump("llcorehttputil_test", true);
        auto handler = std::make_shared<ReplyHandler>(pump);

        // The reply comes once this, the main coroutine, waits for it.
        LLCoros::instance().launch("llcorehttputil_test reply",
                                   [handler]()
                                   {
                                       boost::this_fiber::yield();
                                       deliver(*handler);
                                   });
        const LLSD reply = handler->suspendUntilReply();
        CHECK(reply["answer"].asInteger() == 42);
        CHECK(reply[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS][LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_SUCCESS].asBoolean());
    }

    TEST_CASE("llcorehttputil: a reply its coroutine no longer waits for goes nowhere", "[llcorehttputil]")
    {
        // The coroutine is stopped, as the viewer quits, and takes its pump with it while
        // the reply is parsed on the General queue. By the time the reply arrives, the
        // pump's name is the next coroutine's, waiting on a request of its own.
        auto first = std::make_unique<LLEventStream>("llcorehttputil_test", true);
        const std::string name = first->getName();
        auto handler = std::make_shared<ReplyHandler>(*first);
        first.reset();

        LLEventStream second("llcorehttputil_test", true);
        REQUIRE(second.getName() == name);
        LLSD heard;
        LLTempBoundListener listening(second.listen("llcorehttputil_test",
                                                    [&heard](const LLSD& event)
                                                    {
                                                        heard = event;
                                                        return false;
                                                    }));

        deliver(*handler);
        CHECK(heard.isUndefined());
    }
}
