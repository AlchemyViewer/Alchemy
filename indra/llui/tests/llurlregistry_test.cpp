/**
 * @file llurlregistry_test.cpp
 * @brief The first Url the registry finds in a text, among every entry's.
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

#include "../llurlregistry.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct llurlregistry_data
    {
        // A match's style is made with the UI's fonts.
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        // The first Url in the text, from where it starts to where it ends.
        static std::string found(const char* text)
        {
            LLUrlMatch match;
            if (!LLUrlRegistry::instance().findUrl(text, match))
            {
                return "";
            }
            return std::string(text).substr(match.getStart(), match.getEnd() - match.getStart() + 1);
        }
    };

    // The registry tries only the entries whose patterns its set says match
    // somewhere in the text, so what it finds has to be what trying them
    // all found: the earliest match, ties to the entry registered first.
    TEST_CASE_METHOD(llurlregistry_data, "llurlregistry: findUrl finds the first Url in a text whichever entry's it is", "[llurlregistry]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        CHECK(found("hello http://www.example.com/x there") == std::string("http://www.example.com/x"));
        CHECK(found("see https://secondlife.com/a or http://example.net/b") == std::string("https://secondlife.com/a"));
        UNSCOPED_INFO("a bare Secondlife host");
        CHECK(found("see https://secondlife.com and go") == std::string("https://secondlife.com"));
        UNSCOPED_INFO("a host with a port is the plain Url's");
        CHECK(found("at http://secondlife.com:8080 now") == std::string("http://secondlife.com:8080"));
        UNSCOPED_INFO("a full stop after one is not its");
        CHECK(found("visit http://example.com/index.php.") == std::string("http://example.com/index.php"));
        CHECK(found("mail someone@example.org now") == std::string("someone@example.org"));
        CHECK(found("go SECONDLIFE:///app/chat/42/hello there") == std::string("SECONDLIFE:///app/chat/42/hello"));
        CHECK(found("the :// alone, and www. alone") == std::string(""));
        CHECK(found("nothing to see here") == std::string(""));
        UNSCOPED_INFO("nor where an at or a .com is only a word's");
        CHECK(found("meet me @ the club, the .com boom is over") == std::string(""));
    }

    TEST_CASE_METHOD(llurlregistry_data, "llurlregistry: findUrl finds a nolink tag in any case", "[llurlregistry]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // A check before the patterns once took only a lowercase tag, where
        // the entry's pattern takes one in any case.
        CHECK(found("say <NOLINK>hello</NOLINK> ok") == std::string("<NOLINK>hello</NOLINK>"));
        CHECK(found("say <nolink>hello</nolink> ok") == std::string("<nolink>hello</nolink>"));
    }
}
