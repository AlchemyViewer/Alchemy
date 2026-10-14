/**
 * @file alscriptsnippets_test.cpp
 * @brief The scripter's own snippets, read and written, and a file that does not read kept.
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

#include "../alscriptsnippets.h"

#include "alfilewrite.h"
#include "alsnippetsession.h"

#include "fsyspath.h"
#include "llfile.h"
#include "lluuid.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

#if !LL_WINDOWS
#include <unistd.h>
#endif

namespace
{
    struct alscriptsnippets_data
    {
        std::string folder;

        alscriptsnippets_data()
        {
            folder = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alscriptsnippets_" + LLUUID::generateNewID().asString())).string();
            std::error_code ec;
            std::filesystem::create_directories(fsyspath(folder), ec);
        }
        ~alscriptsnippets_data()
        {
            std::error_code ec;
            std::filesystem::remove_all(fsyspath(folder), ec);
        }

        static std::string read(const std::string& file)
        {
            llifstream        in(file, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }

        static ALScriptSnippets::Snippet snippet(const char* name, const char* body)
        {
            ALScriptSnippets::Snippet one;
            one.name = name;
            one.body = body;
            return one;
        }

        // Whether a stretch closes every bracket it opens, and none it did
        // not.
        static bool whole(std::string_view text)
        {
            std::string open;
            for (const char c : text)
            {
                if (c == '(' || c == '[' || c == '{')
                {
                    open.push_back(c);
                }
                else if (c == ')' || c == ']' || c == '}')
                {
                    const char opener = c == ')' ? '(' : c == ']' ? '[' : '{';
                    if (open.empty() || open.back() != opener)
                    {
                        return false;
                    }
                    open.pop_back();
                }
            }
            return open.empty();
        }
    };

    TEST_CASE_METHOD(alscriptsnippets_data, "alscriptsnippets: snippets read back as written and one with no body yet stays out", "[alscriptsnippets]")
    {
        const std::string file = folder + "/lsl.xml";
        std::vector<ALScriptSnippets::Snippet> none;
        {
            INFO("no file is no snippets, and nothing wrong");
            CHECK(ALScriptSnippets::readFrom(file, false, none));
            REQUIRE(none.empty());
        }
        CHECK(ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"$1\");"), snippet("half", "") }));
        std::vector<ALScriptSnippets::Snippet> back;
        REQUIRE(ALScriptSnippets::readFrom(file, false, back));
        {
            INFO("the one with a body");
            REQUIRE(back.size() == 1);
            CHECK(back[0].name == "say");
            CHECK(back[0].body == "llSay(0, \"$1\");");
        }
    }

    TEST_CASE_METHOD(alscriptsnippets_data, "alscriptsnippets: a file that does not read is kept beside and not written over", "[alscriptsnippets]")
    {
        const std::string file   = folder + "/lsl.xml";
        const std::string broken = "<llsd><array><map><key>name</key><string>mine</string></map>  <!-- a comma too far -->";
        {
            llofstream out(file, std::ios::binary);
            out << broken;
        }
        std::vector<ALScriptSnippets::Snippet> held;
        CHECK_FALSE(ALScriptSnippets::readFrom(file, false, held));
        UNSCOPED_INFO("written all the same");
        CHECK(ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"hi\");") }));
        {
            INFO("what it held kept, as it was");
            CHECK(LLFile::isfile(file + ".unreadable"));
            CHECK(read(file + ".unreadable") == broken);
        }
        std::vector<ALScriptSnippets::Snippet> back;
        {
            INFO("the new file reads");
            CHECK(ALScriptSnippets::readFrom(file, false, back));
            REQUIRE(back.size() == 1);
        }

        // Broken again: kept beside the first, which is not written over.
        {
            llofstream out(file, std::ios::binary);
            out << "again";
        }
        CHECK(ALScriptSnippets::writeTo(file, { snippet("say", "x") }));
        UNSCOPED_INFO("the first kept still");
        CHECK(read(file + ".unreadable") == broken);
        UNSCOPED_INFO("the second beside it");
        CHECK(read(file + ".unreadable.1") == "again");
    }

    TEST_CASE_METHOD(alscriptsnippets_data, "alscriptsnippets: a write replaces the file whole and a failed one leaves it as it was", "[alscriptsnippets]")
    {
        const std::string file = folder + "/lsl.xml";
        CHECK(ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"one\");") }));
        const std::string first = read(file);
        UNSCOPED_INFO("written again, over it");
        CHECK(ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"two\");") }));
        {
            INFO("replaced");
            CHECK(read(file) != first);
            CHECK(read(file).find("two") != std::string::npos);
        }
        UNSCOPED_INFO("and nothing half-written beside it");
        CHECK_FALSE(LLFile::isfile(ALFileWrite::besideOf(file)));

        // A file that cannot be written is not touched: never opened for
        // writing and left cut short.
#if !LL_WINDOWS
        if (geteuid() == 0)
        {
            SKIP("root writes what it likes");
        }
#endif
        const std::string written = read(file);
        std::error_code   ec;
        std::filesystem::permissions(fsyspath(file), std::filesystem::perms::owner_read, std::filesystem::perm_options::replace, ec);
        UNSCOPED_INFO("a file nothing can be written in");
        REQUIRE_FALSE(ec);
        const bool wrote = ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"three\");") });
        std::filesystem::permissions(fsyspath(file), std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace, ec);
        CHECK_FALSE(wrote);
        UNSCOPED_INFO("and the file as it was");
        CHECK(read(file) == written);
    }

    TEST_CASE_METHOD(alscriptsnippets_data, "alscriptsnippets: a notecard carries both languages and passes over keys it does not know", "[alscriptsnippets]")
    {
        ALScriptSnippets::Snippet say = snippet("say", "llSay(0, \"${1:hello}\");");
        say.prefix                    = "say";
        say.detail                    = "Say something";
        ALScriptSnippets::Snippet builtin = snippet("builtin", "not given");
        builtin.builtin                   = true;
        const std::string text = ALScriptSnippets::notecardText({ say, builtin, snippet("", "no name") }, { snippet("print", "print(${1})") });
        std::vector<ALScriptSnippets::Snippet> lsl;
        std::vector<ALScriptSnippets::Snippet> slua;
        REQUIRE(ALScriptSnippets::readNotecard(text, false, lsl, slua));
        UNSCOPED_INFO("the LSL one, and not the viewer's or the unnamed");
        REQUIRE(lsl.size() == 1U);
        CHECK(lsl[0].name == std::string("say"));
        CHECK(lsl[0].prefix == std::string("say"));
        CHECK(lsl[0].detail == std::string("Say something"));
        CHECK(lsl[0].body == say.body);
        CHECK_FALSE(lsl[0].builtin);
        UNSCOPED_INFO("the SLua one in SLua's");
        REQUIRE(slua.size() == 1U);
        CHECK(slua[0].body == std::string("print(${1})"));

        // Written by hand or by an older viewer: no language, and a key
        // this one does not know.
        const std::string plain = "<llsd><array><map><key>name</key><string>loop</string><key>body</key><string>for</string>"
                                  "<key>colour</key><string>red</string></map></array></llsd>";
        lsl.clear();
        slua.clear();
        CHECK(ALScriptSnippets::readNotecard(plain, true, lsl, slua));
        {
            INFO("into the language shown");
            REQUIRE(lsl.empty());
            REQUIRE(slua.size() == 1);
            CHECK(slua[0].name == "loop");
        }
    }

    TEST_CASE_METHOD(alscriptsnippets_data, "alscriptsnippets: a notecard that is not snippets reads as nothing and says so", "[alscriptsnippets]")
    {
        std::vector<ALScriptSnippets::Snippet> lsl;
        std::vector<ALScriptSnippets::Snippet> slua;
        for (const char* text : { "Dear diary, today I scripted a door.", "<llsd><map><key>name</key><string>x</string></map></llsd>",
                                  "<llsd><array><map><key>title</key><string>x</string></map></array></llsd>", "<llsd><array /></llsd>", "" })
        {
            CAPTURE(text);
            REQUIRE_FALSE(ALScriptSnippets::readNotecard(text, false, lsl, slua));
            {
                INFO("and nothing added");
                REQUIRE(lsl.empty());
                REQUIRE(slua.empty());
            }
        }
    }

    TEST_CASE_METHOD(alscriptsnippets_data, "alscriptsnippets: merge leaves out the same snippet and numbers one whose name is taken", "[alscriptsnippets]")
    {
        std::vector<ALScriptSnippets::Snippet> own = { snippet("say", "llSay(0, \"a\");"), snippet("loop", "for") };
        const std::vector<ALScriptSnippets::Snippet> incoming = { snippet("say", "llSay(0, \"b\");"), snippet("loop", "for"),
                                                                  snippet("new", "x") };
        ALScriptSnippets::Merged merged = ALScriptSnippets::merge(own, incoming);
        UNSCOPED_INFO("one added under its own name");
        CHECK(merged.added == 1U);
        UNSCOPED_INFO("one under a number");
        CHECK(merged.renamed == 1U);
        UNSCOPED_INFO("one the same");
        CHECK(merged.skipped == 1U);
        REQUIRE(own.size() == 4U);
        CHECK(own[2].name == std::string("say (2)"));
        CHECK(own[2].body == std::string("llSay(0, \"b\");"));
        CHECK(own[3].name == std::string("new"));

        // The same notecard again brings nothing in.
        merged = ALScriptSnippets::merge(own, incoming);
        {
            INFO("nothing again");
            CHECK(merged.added == 0);
            CHECK(merged.renamed == 0);
            CHECK(merged.skipped == 3);
        }
        REQUIRE(own.size() == 4U);

        // A third body takes the next number free.
        merged = ALScriptSnippets::merge(own, { snippet("say", "llSay(0, \"c\");") });
        {
            INFO("numbered past the taken");
            CHECK(merged.renamed == 1U);
            CHECK(own.back().name == std::string("say (3)"));
        }
    }

    TEST_CASE_METHOD(alscriptsnippets_data, "alscriptsnippets: a followed notecard's snippets go to their languages and are never saved", "[alscriptsnippets]")
    {
        const std::string text = "<llsd><array>"
                                 "<map><key>name</key><string>say</string><key>body</key><string>llSay(0, \"\");</string><key>language</key><string>lsl</string></map>"
                                 "<map><key>name</key><string>loop</string><key>body</key><string>for</string><key>language</key><string>slua</string></map>"
                                 "<map><key>name</key><string>banner</string><key>body</key><string>x</string></map>"
                                 "</array></llsd>";
        CHECK(ALScriptSnippets::follow(text));
        const std::vector<ALScriptSnippets::Snippet>& lsl  = ALScriptSnippets::followed(false);
        const std::vector<ALScriptSnippets::Snippet>& slua = ALScriptSnippets::followed(true);
        {
            INFO("LSL's and the one saying none");
            REQUIRE(lsl.size() == 2);
            CHECK(lsl[0].name == "say");
            CHECK(lsl[1].name == "banner");
        }
        {
            INFO("SLua's and the one saying none");
            REQUIRE(slua.size() == 2);
            CHECK(slua[0].name == "loop");
            CHECK(slua[1].name == "banner");
        }
        {
            INFO("each the notecard's");
            CHECK(lsl[0].followed);
            CHECK(lsl[1].followed);
            CHECK(slua[0].followed);
            CHECK_FALSE(lsl[0].builtin);
        }

        UNSCOPED_INFO("not snippets: not taken");
        CHECK_FALSE(ALScriptSnippets::follow("Dear diary"));
        UNSCOPED_INFO("those before stay");
        REQUIRE(ALScriptSnippets::followed(false).size() == 2);

        // Never written where the scripter's own are, nor carried by their
        // notecard; copied in, one's own.
        const std::string file = folder + "/lsl.xml";
        CHECK(ALScriptSnippets::writeTo(file, { snippet("mine", "y"), lsl[0] }));
        std::vector<ALScriptSnippets::Snippet> back;
        REQUIRE(ALScriptSnippets::readFrom(file, false, back));
        {
            INFO("one's own alone");
            REQUIRE(back.size() == 1);
            CHECK(back[0].name == "mine");
        }
        UNSCOPED_INFO("not on one's notecard");
        CHECK(ALScriptSnippets::notecardText(lsl, {}).find("banner") == std::string::npos);
        std::vector<ALScriptSnippets::Snippet> own;
        ALScriptSnippets::merge(own, { lsl[1] });
        {
            INFO("copied in: one's own");
            REQUIRE(own.size() == 1);
            CHECK_FALSE(own[0].followed);
        }

        UNSCOPED_INFO("none followed");
        CHECK(ALScriptSnippets::follow(std::string()));
        {
            INFO("none left");
            REQUIRE(ALScriptSnippets::followed(false).empty());
            REQUIRE(ALScriptSnippets::followed(true).empty());
        }
    }

    TEST_CASE_METHOD(alscriptsnippets_data, "alscriptsnippets: every shipped snippet expands with whole stops and no placeholder left", "[alscriptsnippets]")
    {
        for (const char* language : { "lsl.xml", "slua.xml" })
        {
            const std::string                      file = std::string(LLUI_TEST_APP_DIR) + "/app_settings/snippets/" + language;
            CAPTURE(file);
            std::vector<ALScriptSnippets::Snippet> shipped;
            REQUIRE(ALScriptSnippets::readFrom(file, true, shipped));
            REQUIRE_FALSE(shipped.empty());
            for (const ALScriptSnippets::Snippet& one : shipped)
            {
                CAPTURE(one.name);
                const ALSnippetSession::Expansion expanded = ALSnippetSession::expand(one.body, ALTextPos(0, 0), std::string());
                const ALTextDocument              text(expanded.text);
                UNSCOPED_INFO("no placeholder left in " << expanded.text);
                REQUIRE(expanded.text.find("${") == std::string::npos);
                for (size_t i = 0; i < expanded.stops.size(); ++i)
                {
                    const std::string held = text.text(expanded.stops[i]);
                    UNSCOPED_INFO("stop " << i + 1 << " holds the whole of its text, not \"" << held << "\"");
                    REQUIRE(whole(held));
                }
            }
        }
    }
}
