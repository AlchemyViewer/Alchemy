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

#include "fsyspath.h"
#include "llfile.h"
#include "lluuid.h"

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#if !LL_WINDOWS
#include <unistd.h>
#endif

namespace tut
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
    };

    typedef test_group<alscriptsnippets_data> alscriptsnippets_group;
    typedef alscriptsnippets_group::object    alscriptsnippets_object;
    alscriptsnippets_group                    alscriptsnippets_instance("alscriptsnippets");

    template<> template<>
    void alscriptsnippets_object::test<1>()
    {
        set_test_name("written and read back; one being written, with no body yet, stays out of the file");
        const std::string file = folder + "/lsl.xml";
        std::vector<ALScriptSnippets::Snippet> none;
        ensure("no file is no snippets, and nothing wrong", ALScriptSnippets::readFrom(file, false, none) && none.empty());
        ensure("written", ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"$1\");"), snippet("half", "") }));
        std::vector<ALScriptSnippets::Snippet> back;
        ensure("read", ALScriptSnippets::readFrom(file, false, back));
        ensure("the one with a body", back.size() == 1 && back[0].name == "say" && back[0].body == "llSay(0, \"$1\");");
    }

    template<> template<>
    void alscriptsnippets_object::test<2>()
    {
        set_test_name("a file that does not read as snippets is kept beside, not written over");
        const std::string file   = folder + "/lsl.xml";
        const std::string broken = "<llsd><array><map><key>name</key><string>mine</string></map>  <!-- a comma too far -->";
        {
            llofstream out(file, std::ios::binary);
            out << broken;
        }
        std::vector<ALScriptSnippets::Snippet> held;
        ensure("it does not read", !ALScriptSnippets::readFrom(file, false, held));
        ensure("written all the same", ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"hi\");") }));
        ensure("what it held kept, as it was", LLFile::isfile(file + ".unreadable") && read(file + ".unreadable") == broken);
        std::vector<ALScriptSnippets::Snippet> back;
        ensure("the new file reads", ALScriptSnippets::readFrom(file, false, back) && back.size() == 1);

        // Broken again: kept beside the first, which is not written over.
        {
            llofstream out(file, std::ios::binary);
            out << "again";
        }
        ensure("written", ALScriptSnippets::writeTo(file, { snippet("say", "x") }));
        ensure("the first kept still", read(file + ".unreadable") == broken);
        ensure("the second beside it", read(file + ".unreadable.1") == "again");
    }

    template<> template<>
    void alscriptsnippets_object::test<3>()
    {
        set_test_name("written whole in the file's place: nothing is left beside it, and a write that fails leaves the file as it was");
        const std::string file = folder + "/lsl.xml";
        ensure("written", ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"one\");") }));
        const std::string first = read(file);
        ensure("written again, over it", ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"two\");") }));
        ensure("replaced", read(file) != first && read(file).find("two") != std::string::npos);
        ensure("and nothing half-written beside it", !LLFile::isfile(ALFileWrite::besideOf(file)));

        // A file that cannot be written is not touched: never opened for
        // writing and left cut short. Root writes what it likes.
#if !LL_WINDOWS
        if (geteuid() == 0)
        {
            return;
        }
#endif
        const std::string written = read(file);
        std::error_code   ec;
        std::filesystem::permissions(fsyspath(file), std::filesystem::perms::owner_read, std::filesystem::perm_options::replace, ec);
        ensure("a file nothing can be written in", !ec);
        const bool wrote = ALScriptSnippets::writeTo(file, { snippet("say", "llSay(0, \"three\");") });
        std::filesystem::permissions(fsyspath(file), std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace, ec);
        ensure("not written", !wrote);
        ensure("and the file as it was", read(file) == written);
    }

    template<> template<>
    void alscriptsnippets_object::test<4>()
    {
        set_test_name("a notecard carries both languages and reads back into each; keys it does not know are passed over");
        ALScriptSnippets::Snippet say = snippet("say", "llSay(0, \"${1:hello}\");");
        say.prefix                    = "say";
        say.detail                    = "Say something";
        ALScriptSnippets::Snippet builtin = snippet("builtin", "not given");
        builtin.builtin                   = true;
        const std::string text = ALScriptSnippets::notecardText({ say, builtin, snippet("", "no name") }, { snippet("print", "print(${1})") });
        std::vector<ALScriptSnippets::Snippet> lsl;
        std::vector<ALScriptSnippets::Snippet> slua;
        ensure("read", ALScriptSnippets::readNotecard(text, false, lsl, slua));
        ensure_equals("the LSL one, and not the viewer's or the unnamed", lsl.size(), 1U);
        ensure_equals("its name", lsl[0].name, std::string("say"));
        ensure_equals("its prefix", lsl[0].prefix, std::string("say"));
        ensure_equals("its detail", lsl[0].detail, std::string("Say something"));
        ensure_equals("its body", lsl[0].body, say.body);
        ensure("not the viewer's", !lsl[0].builtin);
        ensure_equals("the SLua one in SLua's", slua.size(), 1U);
        ensure_equals("its body", slua[0].body, std::string("print(${1})"));

        // Written by hand or by an older viewer: no language, and a key
        // this one does not know.
        const std::string plain = "<llsd><array><map><key>name</key><string>loop</string><key>body</key><string>for</string>"
                                  "<key>colour</key><string>red</string></map></array></llsd>";
        lsl.clear();
        slua.clear();
        ensure("read", ALScriptSnippets::readNotecard(plain, true, lsl, slua));
        ensure("into the language shown", lsl.empty() && slua.size() == 1 && slua[0].name == "loop");
    }

    template<> template<>
    void alscriptsnippets_object::test<5>()
    {
        set_test_name("a notecard that is not snippets reads as nothing, and says so");
        std::vector<ALScriptSnippets::Snippet> lsl;
        std::vector<ALScriptSnippets::Snippet> slua;
        for (const char* text : { "Dear diary, today I scripted a door.", "<llsd><map><key>name</key><string>x</string></map></llsd>",
                                  "<llsd><array><map><key>title</key><string>x</string></map></array></llsd>", "<llsd><array /></llsd>", "" })
        {
            ensure(std::string("not snippets: ") + text, !ALScriptSnippets::readNotecard(text, false, lsl, slua));
            ensure("and nothing added", lsl.empty() && slua.empty());
        }
    }

    template<> template<>
    void alscriptsnippets_object::test<6>()
    {
        set_test_name("brought in beside one's own: the same left out, a name taken by another body numbered, and again left out");
        std::vector<ALScriptSnippets::Snippet> own = { snippet("say", "llSay(0, \"a\");"), snippet("loop", "for") };
        const std::vector<ALScriptSnippets::Snippet> incoming = { snippet("say", "llSay(0, \"b\");"), snippet("loop", "for"),
                                                                  snippet("new", "x") };
        ALScriptSnippets::Merged merged = ALScriptSnippets::merge(own, incoming);
        ensure_equals("one added under its own name", merged.added, 1U);
        ensure_equals("one under a number", merged.renamed, 1U);
        ensure_equals("one the same", merged.skipped, 1U);
        ensure_equals("four now", own.size(), 4U);
        ensure_equals("numbered", own[2].name, std::string("say (2)"));
        ensure_equals("with its body", own[2].body, std::string("llSay(0, \"b\");"));
        ensure_equals("and the new one", own[3].name, std::string("new"));

        // The same notecard again brings nothing in.
        merged = ALScriptSnippets::merge(own, incoming);
        ensure("nothing again", merged.added == 0 && merged.renamed == 0 && merged.skipped == 3);
        ensure_equals("still four", own.size(), 4U);

        // A third body takes the next number free.
        merged = ALScriptSnippets::merge(own, { snippet("say", "llSay(0, \"c\");") });
        ensure_equals("numbered past the taken", own.back().name, std::string("say (3)"));
    }

    template<> template<>
    void alscriptsnippets_object::test<7>()
    {
        set_test_name("a followed notecard's snippets: each in its language, one saying none in both, taken in place of those before; text that is not snippets taken as nothing; never written as one's own");
        const std::string text = "<llsd><array>"
                                 "<map><key>name</key><string>say</string><key>body</key><string>llSay(0, \"\");</string><key>language</key><string>lsl</string></map>"
                                 "<map><key>name</key><string>loop</string><key>body</key><string>for</string><key>language</key><string>slua</string></map>"
                                 "<map><key>name</key><string>banner</string><key>body</key><string>x</string></map>"
                                 "</array></llsd>";
        ensure("followed", ALScriptSnippets::follow(text));
        const std::vector<ALScriptSnippets::Snippet>& lsl  = ALScriptSnippets::followed(false);
        const std::vector<ALScriptSnippets::Snippet>& slua = ALScriptSnippets::followed(true);
        ensure("LSL's and the one saying none", lsl.size() == 2 && lsl[0].name == "say" && lsl[1].name == "banner");
        ensure("SLua's and the one saying none", slua.size() == 2 && slua[0].name == "loop" && slua[1].name == "banner");
        ensure("each the notecard's", lsl[0].followed && lsl[1].followed && slua[0].followed && !lsl[0].builtin);

        ensure("not snippets: not taken", !ALScriptSnippets::follow("Dear diary"));
        ensure("those before stay", ALScriptSnippets::followed(false).size() == 2);

        // Never written where the scripter's own are, nor carried by their
        // notecard; copied in, one's own.
        const std::string file = folder + "/lsl.xml";
        ensure("written", ALScriptSnippets::writeTo(file, { snippet("mine", "y"), lsl[0] }));
        std::vector<ALScriptSnippets::Snippet> back;
        ensure("read", ALScriptSnippets::readFrom(file, false, back));
        ensure("one's own alone", back.size() == 1 && back[0].name == "mine");
        ensure("not on one's notecard", ALScriptSnippets::notecardText(lsl, {}).find("banner") == std::string::npos);
        std::vector<ALScriptSnippets::Snippet> own;
        ALScriptSnippets::merge(own, { lsl[1] });
        ensure("copied in: one's own", own.size() == 1 && !own[0].followed);

        ensure("none followed", ALScriptSnippets::follow(std::string()));
        ensure("none left", ALScriptSnippets::followed(false).empty() && ALScriptSnippets::followed(true).empty());
    }
}
