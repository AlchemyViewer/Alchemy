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

#include "../test/lltut.h"

class LLAvatarName;
const std::string gUrlRegistryTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gUrlRegistryTestAnonName;
}

namespace tut
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

    typedef test_group<llurlregistry_data> llurlregistry_group;
    typedef llurlregistry_group::object    llurlregistry_object;
    llurlregistry_group                    llurlregistry_instance("llurlregistry");

    // The registry tries only the entries whose patterns its set says match
    // somewhere in the text, so what it finds has to be what trying them
    // all found: the earliest match, ties to the entry registered first.
    template<> template<>
    void llurlregistry_object::test<1>()
    {
        set_test_name("the first Url in a text, whichever entry's it is, and none where there is none");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ensure_equals("a Url in the text", found("hello http://www.example.com/x there"), std::string("http://www.example.com/x"));
        ensure_equals("the first of two", found("see https://secondlife.com/a or http://example.net/b"), std::string("https://secondlife.com/a"));
        ensure_equals("a bare Secondlife host", found("see https://secondlife.com and go"), std::string("https://secondlife.com"));
        ensure_equals("a host with a port is the plain Url's", found("at http://secondlife.com:8080 now"), std::string("http://secondlife.com:8080"));
        ensure_equals("a full stop after one is not its", found("visit http://example.com/index.php."), std::string("http://example.com/index.php"));
        ensure_equals("an address", found("mail someone@example.org now"), std::string("someone@example.org"));
        ensure_equals("an app Url in any case", found("go SECONDLIFE:///app/chat/42/hello there"), std::string("SECONDLIFE:///app/chat/42/hello"));
        ensure_equals("nothing where there is none", found("the :// alone, and www. alone"), std::string(""));
        ensure_equals("nor in plain words", found("nothing to see here"), std::string(""));
        ensure_equals("nor where an at or a .com is only a word's", found("meet me @ the club, the .com boom is over"), std::string(""));
    }

    template<> template<>
    void llurlregistry_object::test<2>()
    {
        set_test_name("a text is asked of every entry as its pattern reads, in any case, with nothing else Url-like in it");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // A check before the patterns once took only a lowercase tag, where
        // the entry's pattern takes one in any case.
        ensure_equals("a nolink tag in capitals", found("say <NOLINK>hello</NOLINK> ok"), std::string("<NOLINK>hello</NOLINK>"));
        ensure_equals("and in lowercase", found("say <nolink>hello</nolink> ok"), std::string("<nolink>hello</nolink>"));
    }
}
