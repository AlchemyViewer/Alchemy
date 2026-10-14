/**
 * @file alscriptstudiowords_test.cpp
 * @brief Script Studio's words: the vocabulary from the definitions, editors taught it, hovers, completions and the reference.
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

#include "../alscriptstudiowords.h"

#include "alscriptstudio_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

namespace
{
    typedef ALScriptStudioWords       Words;
    typedef ALScriptStudioWords::Vocab Vocab;

    LLSD argument(const std::string& name, const std::string& type)
    {
        return LLSD().with(name, LLSD().with("type", type));
    }

    // Definitions as the grid gives them: LSL's and SLua's.
    LLSD lslKeywords()
    {
        LLSD keywords;
        const LLSD say_arguments       = LLSD().with(0, argument("channel", "integer")).with(1, argument("msg", "string"));
        keywords["functions"]["llSay"] =
            LLSD().with("return", "").with("arguments", say_arguments).with("tooltip", "Says msg on channel.\nMore.");
        keywords["functions"]["llOld"] = LLSD().with("return", "integer").with("deprecated", true).with("tooltip", "Gone.");
        keywords["events"]["touch_start"] = LLSD().with("arguments", LLSD().with(0, argument("num_detected", "integer")));
        keywords["constants"]["PI"]       = LLSD().with("type", "float").with("value", "3.14159265").with("tooltip", "The ratio.");
        keywords["types"]["integer"]      = LLSD().with("tooltip", "A whole number.\nMore.");
        keywords["controls"]["jump"]      = LLSD().with("tooltip", "Goes to a label.");
        keywords["somethingElse"]["x"]    = LLSD();
        return keywords;
    }
    LLSD sluaKeywords()
    {
        LLSD keywords;
        keywords["functions"]["ll.Say"]   = LLSD().with("return", "()").with("arguments", LLSD().with(0, argument("channel", "number")));
        keywords["functions"]["math.pi"]  = LLSD().with("return", "number");
        keywords["events"]["touch_start"] = LLSD().with("arguments", LLSD().with(0, argument("detected", "{DetectedEvent}")));
        keywords["events"]["listen"] = LLSD().with("arguments", LLSD()
                                                                    .with(0, argument("channel", "number"))
                                                                    .with(1, argument("name", "string"))
                                                                    .with(2, argument("id", "uuid"))
                                                                    .with(3, argument("msg", "string")));
        return keywords;
    }
}

namespace
{
    struct alscriptstudiowords_data
    {
        // The viewer the units ask, attached first and let go of last.
        al_studio_test::StudioViewer viewer;
        al_studio_test::StudioWindow window;
        std::vector<std::string>     extra;
        LLSD                         lsl  = lslKeywords();
        LLSD                         slua = sluaKeywords();

        alscriptstudiowords_data()
        {
            auto& sources = viewer.slots;
            sources.keywords          = [this](bool lua) { return lua ? slua : lsl; };
            sources.preprocessorWords = [this] { return extra; };
            sources.lslHelpUrl        = [] { return std::string("https://wiki.example/[LSL_STRING]"); };
            Words::forget();
        }
        ~alscriptstudiowords_data()
        {
            Words::forget();
        }
        ALCodeEditor& editor(const std::string& text, bool lua)
        {
            if (!window.floater)
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name        = "editor";
            p.rect        = LLRect(0, 200, 400, 0);
            p.syntax      = lua ? "slua" : "lsl";
            ALCodeEditor* made = LLUICtrlFactory::create<ALCodeEditor>(p);
            window.floater->addChild(made);
            made->setText(text);
            return *made;
        }
        // A word the definitions give, for a case to read: one missing ends
        // the case here rather than reading through nothing. Asked before an
        // UNSCOPED_INFO, not between it and its check, which this REQUIRE
        // passing would take the message from.
        static const Vocab& known(bool lua, const std::string& name)
        {
            const Vocab* found = Words::word(lua, name);
            CAPTURE(name);
            REQUIRE(found);
            return *found;
        }
        static std::vector<std::string> texts(const std::vector<ALCodeEditor::Completion>& completions)
        {
            std::vector<std::string> out;
            for (const ALCodeEditor::Completion& c : completions)
            {
                out.push_back(c.text);
            }
            return out;
        }
    };

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: the vocabulary gives each kind's detail sorted and skips unknown groups", "[alscriptstudiowords]")
    {
        const std::vector<Vocab>& words = Words::vocabulary(false);
        CHECK(words.size() == size_t(6));
        CHECK(std::is_sorted(words.begin(), words.end(), [](const Vocab& a, const Vocab& b) { return a.text < b.text; }));
        const Vocab* say = Words::word(false, "llSay");
        {
            INFO("a function's call, with its arguments' types");
            REQUIRE(say);
            CHECK(say->kind == ALSyntaxKind::Function);
            CHECK(say->detail == "llSay(integer channel, string msg)");
        }
        {
            INFO("and what returns");
            const Vocab& old = known(false, "llOld");
            CHECK(old.detail == "integer llOld()");
            CHECK(old.deprecated);
        }
        const Vocab& touch = known(false, "touch_start");
        UNSCOPED_INFO("an event's");
        CHECK(touch.detail == std::string("touch_start(integer num_detected)"));
        const Vocab& pi = known(false, "PI");
        UNSCOPED_INFO("a constant's type and value");
        CHECK(pi.detail == std::string("float PI = 3.14159265"));
        const Vocab& integer = known(false, "integer");
        UNSCOPED_INFO("a type's tooltip's first line");
        CHECK(integer.detail == std::string("A whole number."));
        const Vocab& jump = known(false, "jump");
        CHECK(jump.kind == ALSyntaxKind::Control);
        {
            INFO("not a word");
            CHECK_FALSE(Words::word(false, "x"));
            CHECK_FALSE(Words::word(false, ""));
            CHECK_FALSE(Words::word(false, "nothing"));
        }
        const Vocab& slua_say = known(true, "ll.Say");
        UNSCOPED_INFO("SLua's () returns nothing worth saying");
        CHECK(slua_say.detail == std::string("ll.Say(number channel)"));
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: the vocabulary is kept until forgotten and then built again", "[alscriptstudiowords]")
    {
        REQUIRE(Words::word(false, "llSay") != nullptr);
        lsl["functions"].erase("llSay");
        lsl["functions"]["llShout"] = LLSD().with("return", "");
        {
            INFO("kept");
            CHECK(Words::word(false, "llSay") != nullptr);
            CHECK_FALSE(Words::word(false, "llShout"));
        }
        Words::forget();
        {
            INFO("built again");
            CHECK_FALSE(Words::word(false, "llSay"));
            CHECK(Words::word(false, "llShout") != nullptr);
        }
        viewer.slots.keywords = [](bool) { return LLSD("not a map"); };
        Words::forget();
        {
            INFO("definitions that are not: no words");
            CHECK(Words::vocabulary(false).empty());
            CHECK_FALSE(Words::word(false, "llShout"));
        }
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: an editor is taught each kind's table and the preprocessor's words", "[alscriptstudiowords]")
    {
        ALCodeEditor& e = editor("default {}", false);
        extra           = { "switch", "case" };
        Words::teach(e, false);
        const ALSyntaxWords& tables = e.highlighter().words();
        CHECK(tables.has("function", "llSay"));
        CHECK_FALSE(tables.has("function", "llOld"));
        CHECK(tables.has("deprecated", "llOld"));
        CHECK(tables.has("event", "touch_start"));
        CHECK(tables.has("constant", "PI"));
        CHECK(tables.has("type", "integer"));
        CHECK(tables.has("control", "jump"));
        {
            INFO("the preprocessor's");
            CHECK(tables.has("control", "switch"));
            CHECK(tables.has("control", "case"));
        }
        ALCodeEditor& s = editor("x", true);
        Words::teach(s, true);
        {
            INFO("SLua's own, and none of the preprocessor's");
            CHECK(s.highlighter().words().has("function", "ll.Say"));
            CHECK_FALSE(s.highlighter().words().has("control", "switch"));
        }
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: inStateBody finds a state's body past braces in strings and comments", "[alscriptstudiowords]")
    {
        ALCodeEditor& e = editor("default\n{\n    \n    touch_start(integer n)\n    {\n        \n    }\n}\n"
                                 "state /* named */ two { \"{\" // {\n  \n}\n",
                                 false);
        UNSCOPED_INFO("in default");
        CHECK(Words::inStateBody(e, ALTextPos(2, 4)));
        UNSCOPED_INFO("in a handler: not");
        CHECK_FALSE(Words::inStateBody(e, ALTextPos(5, 8)));
        UNSCOPED_INFO("after default: not");
        CHECK_FALSE(Words::inStateBody(e, ALTextPos(8, 0)));
        UNSCOPED_INFO("in a state named, a comment between, past a brace in a string and a comment");
        CHECK(Words::inStateBody(e, ALTextPos(9, 2)));
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: completions fill in handlers where they go and offer snippets", "[alscriptstudiowords]")
    {
        const ALCodeEditor::Completion lsl = Words::completionFor(known(false, "touch_start"), false);
        UNSCOPED_INFO("LSL's handler");
        CHECK(lsl.snippet == std::string("touch_start(integer num_detected)\n{\n    $0\n}"));
        const Vocab&                   slua_touch = known(true, "touch_start");
        const ALCodeEditor::Completion slua       = Words::completionFor(slua_touch, true);
        UNSCOPED_INFO("SLua's, a function of LLEvents'");
        CHECK(slua.snippet == std::string("function LLEvents.touch_start(detected)\n    $0\nend"));
        UNSCOPED_INFO("typed where asked, as the definitions type it");
        CHECK(Words::completionFor(slua_touch, true, true).snippet ==
              std::string("function LLEvents.touch_start(detected: {DetectedEvent})\n    $0\nend"));
        const Vocab& listen = known(true, "listen");
        UNSCOPED_INFO("each parameter typed");
        CHECK(Words::completionFor(listen, true, true).snippet ==
              std::string("function LLEvents.listen(channel: number, name: string, id: uuid, msg: string)\n    $0\nend"));
        UNSCOPED_INFO("each untyped");
        CHECK(Words::completionFor(listen, true).snippet ==
              std::string("function LLEvents.listen(channel, name, id, msg)\n    $0\nend"));
        const ALCodeEditor::Completion say = Words::completionFor(known(false, "llSay"), false);
        {
            INFO("a function as itself");
            CHECK(say.snippet.empty());
            CHECK(say.text == "llSay");
            REQUIRE(say.documentation);
            CHECK(*say.documentation == "Says msg on channel.\nMore.");
        }

        ALCodeEditor&                         e = editor("default\n{\n    t\n}\nt\n", false);
        ALScriptSnippets::Snippet             snippet;
        snippet.name   = "Timer";
        snippet.prefix = "timer";
        snippet.body   = "llSetTimerEvent($1);";
        std::vector<ALCodeEditor::Completion> out;
        Words::complete(false, e, ALTextPos(2, 5), "t", { snippet }, "Snippet", out);
        UNSCOPED_INFO("inside the state: the handler and the snippet");
        REQUIRE(texts(out) == std::vector<std::string>{ "touch_start", "timer" });
        UNSCOPED_INFO("the snippet said so");
        CHECK(out[1].detail == std::string("Snippet  Timer"));
        out.clear();
        Words::complete(false, e, ALTextPos(4, 1), "t", { snippet }, "Snippet", out);
        UNSCOPED_INFO("outside: no handler");
        CHECK(texts(out) == std::vector<std::string>{ "timer" });
        out.clear();
        ALScriptSnippets::Snippet member = snippet;
        member.prefix                    = "ll.Snip";
        Words::complete(true, e, ALTextPos(4, 1), "ll.S", { member }, "Snippet", out);
        UNSCOPED_INFO("a member: the vocabulary's alone");
        CHECK(texts(out) == std::vector<std::string>{ "ll.Say" });
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: a library's table completes as one, and once where it is a type too", "[alscriptstudiowords]")
    {
        slua["functions"]["lljson"]        = LLSD().with("tooltip", "JSON library.");
        slua["functions"]["lljson.encode"] = LLSD().with("return", "string").with("arguments", LLSD().with(0, argument("value", "any")));
        slua["functions"]["string"]        = LLSD().with("tooltip", "String library.");
        slua["types"]["string"]            = LLSD().with("tooltip", "Text.");
        Words::forget();
        const Vocab& lljson = known(true, "lljson");
        {
            INFO("Tapple's: a table, called by its name alone");
            CHECK(lljson.table);
            CHECK(lljson.detail == "lljson");
        }
        const ALCodeEditor::Completion c = Words::completionFor(lljson, true);
        {
            INFO("completed as a namespace, a dot after it");
            CHECK(c.table);
            CHECK(c.kind == ALSyntaxKind::Namespace);
        }
        UNSCOPED_INFO("a member of it, a function");
        CHECK_FALSE(known(true, "lljson.encode").table);
        UNSCOPED_INFO("one that gives something, a function");
        CHECK_FALSE(known(true, "math.pi").table);

        ALCodeEditor&                         e = editor("str\n", true);
        std::vector<ALCodeEditor::Completion> out;
        Words::complete(true, e, ALTextPos(0, 3), "str", {}, "Snippet", out);
        {
            INFO("a table that is a type too: offered once, as the type");
            REQUIRE(texts(out) == std::vector<std::string>{ "string" });
            CHECK_FALSE(out[0].table);
        }
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: hover text names a member in full and notes the deprecated", "[alscriptstudiowords]")
    {
        ALCodeEditor& s = editor("ll.Say(0)\nx = math.pi\n", true);
        std::string   text;
        {
            INFO("ll.Say under Say");
            CHECK(Words::hoverText(true, s.document(), ALTextPos(0, 4), "Say", text));
            CHECK(text == "ll.Say(number channel)");
        }
        {
            INFO("math.pi under pi");
            CHECK(Words::hoverText(true, s.document(), ALTextPos(1, 10), "pi", text));
            CHECK(text == "number math.pi()");
        }
        UNSCOPED_INFO("a word of the script's");
        CHECK_FALSE(Words::hoverText(true, s.document(), ALTextPos(1, 0), "x", text));
        ALCodeEditor& e = editor("llOld()\n", false);
        {
            INFO("the deprecated");
            CHECK(Words::hoverText(false, e.document(), ALTextPos(0, 2), "llOld", text));
            CHECK(text == "integer llOld()\nGone.\n" + ALCodeEditor::deprecatedNote());
        }
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: the Insert menu's lists and each word's reference page", "[alscriptstudiowords]")
    {
        ALScriptSnippets::Snippet snippet;
        snippet.name   = "Timer";
        snippet.prefix = "timer";
        snippet.detail = "Every so often";
        const std::vector<ALQuickOpen::Candidate> snippets = Words::library(false, "snippet", { snippet }, "Deprecated");
        {
            INFO("a snippet, by what it is");
            REQUIRE(snippets.size() == 1);
            CHECK(snippets[0].value == "Timer\ntimer");
            CHECK(snippets[0].also == "timer");
        }
        const std::vector<ALQuickOpen::Candidate> functions = Words::library(false, "function", {}, "Deprecated");
        {
            INFO("the functions alone");
            REQUIRE(functions.size() == 2);
            CHECK(functions[0].label == "llOld");
            CHECK(functions[1].label == "llSay");
        }
        {
            INFO("the deprecated said so, the rest by their tooltip's first line");
            CHECK(functions[0].detail == "Deprecated");
            CHECK(functions[1].detail == "Says msg on channel.");
        }
        CHECK(Words::library(false, "event", {}, "") .size() == 1);

        UNSCOPED_INFO("an LSL name");
        CHECK(Words::helpUrl(false, "llSay") == std::string("https://wiki.example/llSay"));
        UNSCOPED_INFO("SLua's ll name, as LSL's");
        CHECK(Words::helpUrl(true, "ll.Say") == std::string("https://wiki.example/llSay"));
        UNSCOPED_INFO("a library's");
        CHECK(Words::helpUrl(true, "math.pi") == std::string("https://luau.org/library"));
        UNSCOPED_INFO("the rest");
        CHECK(Words::helpUrl(true, "LLEvents") == std::string("https://wiki.secondlife.com/wiki/Lua_Alpha"));
        UNSCOPED_INFO("nothing: the portal");
        CHECK(Words::helpUrl(false, "") == std::string("https://wiki.example/LSL_Portal"));
        const Vocab& pi = known(false, "PI");
        UNSCOPED_INFO("the reference's words");
        CHECK(Words::referenceText(pi, false) ==
              std::string("float PI = 3.14159265\n\nThe ratio.\nhttps://wiki.example/PI"));
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: a tab's mark follows what it is and a symbol's its kind", "[alscriptstudiowords]")
    {
        ALScriptStudioDoc doc;
        UNSCOPED_INFO("an LSL script");
        CHECK(std::string(Words::imageNameOf(doc)) == std::string("Inv_Script"));
        doc.language.lua = true;
        UNSCOPED_INFO("an SLua one");
        CHECK(std::string(Words::imageNameOf(doc)) == std::string("Inv_Script_Luau"));
        doc.notecard = true;
        UNSCOPED_INFO("a notecard");
        CHECK(std::string(Words::imageNameOf(doc)) == std::string("Inv_Notecard"));
        doc.file = "/a/notes.txt";
        UNSCOPED_INFO("a file");
        CHECK(std::string(Words::imageNameOf(doc)) == std::string("Studio_File"));
        doc.name = ".luaurc";
        UNSCOPED_INFO("a config");
        CHECK(std::string(Words::imageNameOf(doc)) == std::string("Studio_Config"));
        CHECK(std::string(Words::imageNameOf(ALScriptSymbolKind::Function)) == std::string("Symbol_Function"));
        UNSCOPED_INFO("a state as a label");
        CHECK(std::string(Words::imageNameOf(ALScriptSymbolKind::State)) == std::string("Symbol_Label"));
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: each symbol kind has a window word of its own", "[alscriptstudiowords]")
    {
        if (!window.floater)
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::set<std::string> said;
        for (S32 kind = static_cast<S32>(ALScriptSymbolKind::Keyword); kind <= static_cast<S32>(ALScriptSymbolKind::Module); ++kind)
        {
            const char* word = Words::kindWordOf(static_cast<ALScriptSymbolKind>(kind));
            CAPTURE(kind);
            REQUIRE(word != nullptr);
            REQUIRE(window.floater->hasString(word));
            said.insert(window.floater->getString(word));
        }
        UNSCOPED_INFO("each its own");
        CHECK(said.size() == size_t(static_cast<S32>(ALScriptSymbolKind::Module) + 1));
        CHECK(window.floater->getString(Words::kindWordOf(ALScriptSymbolKind::Function)) == std::string("function"));
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: a builtin's notes say its delay and god mode and not for a member", "[alscriptstudiowords]")
    {
        lsl["functions"]["llEmail"]            = LLSD().with("return", "").with("sleep", "20.0").with("tooltip", "Sends an email.");
        lsl["functions"]["llGodLikeRezObject"] = LLSD().with("return", "").with("god-mode", true).with("sleep", "0.0");
        slua["functions"]["type"]              = LLSD().with("return", "string").with("tooltip", "What a value is.");
        Words::forget();
        const Vocab* email = Words::word(false, "llEmail");
        REQUIRE(email);
        CHECK(email->sleep == "20.0");
        CHECK_FALSE(email->godMode);
        const std::string notes = Words::notesOf(*email);
        {
            INFO("its words and its delay: " << notes);
            CHECK(notes.find("Sends an email.") == 0);
            CHECK(notes.find("20.0") != std::string::npos);
        }
        const Vocab* god = Words::word(false, "llGodLikeRezObject");
        {
            INFO("a god's, no delay said for none");
            REQUIRE(god);
            CHECK(god->godMode);
            CHECK(god->sleep.empty());
        }
        UNSCOPED_INFO("that only a god may call it, said");
        CHECK_FALSE(Words::notesOf(*god).empty());
        // `type` under the mouse in `obj.type` is some table's field.
        ALCodeEditor& s = editor("local t = obj.type\nlocal u = type(t)\n", true);
        std::string   text;
        UNSCOPED_INFO("a member: not the global");
        CHECK_FALSE(Words::hoverText(true, s.document(), ALTextPos(0, 15), "type", text));
        {
            INFO("the global itself");
            CHECK(Words::hoverText(true, s.document(), ALTextPos(1, 11), "type", text));
            CHECK(text.find("What a value is.") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: linkArgument finds the link-number argument as the definitions name it", "[alscriptstudiowords]")
    {
        lsl["functions"]["llSetLinkAlpha"] = LLSD().with("return", "").with(
            "arguments",
            LLSD().with(0, argument("LinkNumber", "integer")).with(1, argument("Opacity", "float")).with(2, argument("Face", "integer")));
        slua["functions"]["ll.MessageLinked"] =
            LLSD().with("return", "()").with("arguments", LLSD().with(0, argument("link", "number")).with(1, argument("num", "number")));
        Words::forget();
        CHECK(Words::linkArgument(false, "llSetLinkAlpha", 0));
        CHECK_FALSE(Words::linkArgument(false, "llSetLinkAlpha", 1));
        UNSCOPED_INFO("nor past its last");
        CHECK_FALSE(Words::linkArgument(false, "llSetLinkAlpha", 3));
        CHECK_FALSE(Words::linkArgument(false, "llSay", 0));
        UNSCOPED_INFO("a word not known, none");
        CHECK_FALSE(Words::linkArgument(false, "llNothing", 0));
        {
            INFO("SLua's by its name with its library");
            CHECK(Words::linkArgument(true, "ll.MessageLinked", 0));
            CHECK_FALSE(Words::linkArgument(true, "ll.MessageLinked", 1));
        }
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: word tables are shared per language and rebuilt when their sources change", "[alscriptstudiowords]")
    {
        S32         asked   = 0;
        std::string version = "one";
        viewer.slots.keywords = [this, &asked](bool lua) {
            ++asked;
            return lua ? slua : lsl;
        };
        viewer.slots.definitionsVersion = [&version] { return version; };
        Words::forget();
        ALCodeEditor& a = editor("default {}", false);
        ALCodeEditor& b = editor("default {}", false);
        ALCodeEditor& s = editor("x", true);
        Words::teach(a, false);
        Words::teach(b, false);
        Words::teach(s, true);
        UNSCOPED_INFO("one set for both LSL editors");
        CHECK(&a.highlighter().words() == &b.highlighter().words());
        UNSCOPED_INFO("another for SLua's");
        CHECK(&s.highlighter().words() != &a.highlighter().words());
        const S32 first = asked;
        Words::teach(a, false);
        UNSCOPED_INFO("taught again from what is kept");
        CHECK(asked == first);

        version                     = "two";
        lsl["functions"]["llShout"] = LLSD().with("return", "");
        Words::teach(a, false);
        Words::teach(b, false);
        UNSCOPED_INFO("new definitions read once for both");
        CHECK(asked == first + 1);
        {
            INFO("and shared still, with the new word");
            CHECK(&a.highlighter().words() == &b.highlighter().words());
            CHECK(a.highlighter().words().has("function", "llShout"));
        }

        const ALSyntaxWords* before = &a.highlighter().words();
        extra                       = { "switch" };
        Words::teach(a, false);
        {
            INFO("new preprocessor words, new tables");
            CHECK(&a.highlighter().words() != before);
            CHECK(a.highlighter().words().has("control", "switch"));
        }
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: the reference groups each word by category or library or family", "[alscriptstudiowords]")
    {
        Words::Categories found;
        Words::readCategories("constants:\n"
                              "  PI:\n"
                              "    categories:\n"
                              "      - math\n"
                              "functions:\n"
                              "  llSay:\n"
                              "    arguments:\n"
                              "    - Channel:\n"
                              "        tooltip: Channel to use.\n"
                              "    tooltip: Says Text on Channel.\n"
                              "      More text, six in.\n"
                              "    categories:\n"
                              "      - avatar_communication\n"
                              "      - chat\n"
                              "  llOld:\n"
                              "    tooltip: Gone.\n"
                              "events:\n"
                              "  touch_start:\n"
                              "    categories:\n"
                              "      - touch\n",
                              found);
        UNSCOPED_INFO("only functions, and those with categories");
        CHECK(found.size() == size_t(1));
        UNSCOPED_INFO("by the first");
        CHECK(found["llSay"] == std::string("avatar_communication"));

        viewer.slots.definitionsYaml = [] { return std::string("functions:\n  llSay:\n    categories:\n      - avatar_communication\n"); };
        lsl["constants"]["PRIM_POSITION"] = LLSD().with("type", "integer").with("value", "6");
        Words::forget();
        const Vocab& say = known(false, "llSay");
        UNSCOPED_INFO("LSL, in words");
        CHECK(say.group == std::string("Avatar communication"));
        const Vocab& old = known(false, "llOld");
        UNSCOPED_INFO("none where none is said");
        CHECK(old.group.empty());
        const Vocab& touch = known(false, "touch_start");
        UNSCOPED_INFO("an event none");
        CHECK(touch.group.empty());
        const Vocab& position = known(false, "PRIM_POSITION");
        UNSCOPED_INFO("a constant by family");
        CHECK(position.group == std::string("PRIM_..."));
        const Vocab& pi = known(false, "PI");
        UNSCOPED_INFO("one without a family none");
        CHECK(pi.group.empty());
        const Vocab& slua_say = known(true, "ll.Say");
        UNSCOPED_INFO("SLua's ll function as its twin");
        CHECK(slua_say.group == std::string("Avatar communication"));
        const Vocab& math_pi = known(true, "math.pi");
        UNSCOPED_INFO("another SLua function by library");
        CHECK(math_pi.group == std::string("math"));
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: the units ask the attached viewer and default what it leaves unsaid", "[alscriptstudiowords]")
    {
        ALScriptStudioViewer& asked = ALScriptStudioViewer::get();
        UNSCOPED_INFO("the test's is the one asked");
        CHECK(&asked == &viewer);
        UNSCOPED_INFO("its words");
        CHECK(asked.lslHelpUrl() == "https://wiki.example/[LSL_STRING]");
        CHECK(asked.definitionsYaml().empty());
        CHECK(asked.definitionsVersion().empty());
        CHECK_FALSE(asked.preprocessing());
        CHECK_FALSE(asked.transformOn(ALPreprocessor::Transform::Switch));
        ALPreprocessor::Include found;
        CHECK(asked.lookUp(ALScriptPreprocessor::Request(), ALPreprocessor::Ask(), found) == ALPreprocessor::Found::No);
        viewer.slots.lslHelpUrl = nullptr;
        UNSCOPED_INFO("the wiki as nothing says it");
        CHECK(asked.lslHelpUrl() == std::string("[LSL_STRING]"));
    }

    // The call a string is an argument of, and what kind of item of the
    // object's it names there: however many lines and comments come between
    // the string and the bracket or comma before it; SLua's call of a string
    // alone; nothing for a string that is not an argument's start, nor one
    // of an argument that names no item.
    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: a string argument says its call and the kind of item it names", "[alscriptstudiowords]")
    {
        using Item = ALLSLTraits::Item;
        const auto kind = [this](const std::string& text, bool lua, char* quote = nullptr) {
            ALCodeEditor& e = editor(text, lua);
            // At the end: in the string, not closed.
            const S32 last = e.document().lineCount() - 1;
            char      said = '\0';
            const Item found = Words::itemStringAt(e, ALTextPos(last, static_cast<S32>(e.document().line(last).size())), lua, said);
            if (quote)
            {
                *quote = said;
            }
            return found;
        };
        char quote = '\0';
        {
            INFO("on its line");
            CHECK(kind("llPlaySound(\"do", false, &quote) == Item::Sound);
            CHECK(quote == '"');
        }
        UNSCOPED_INFO("a second argument");
        CHECK(kind("llGiveInventory(llGetOwner(), \"x", false) == Item::Any);
        UNSCOPED_INFO("lines and comments between");
        CHECK(kind("llGiveInventory(llGetOwner(),\n    // what to give\n    /* by name */\n    \"x", false) == Item::Any);
        UNSCOPED_INFO("many lines between");
        CHECK(kind("llRezObject(\n\n\n\n\n\n\"obj", false) == Item::Object);
        {
            INFO("SLua's, by its own name");
            CHECK(kind("ll.StartAnimation('wa", true, &quote) == Item::Animation);
            CHECK(quote == '\'');
        }
        UNSCOPED_INFO("SLua's with no brackets");
        CHECK(kind("ll.PlaySound \"do", true) == Item::Sound);
        UNSCOPED_INFO("and none in LSL, which has no such call");
        CHECK(kind("llPlaySound \"do", false) == Item::None);
        UNSCOPED_INFO("not an argument naming an item");
        CHECK(kind("llSay(0, \"x", false) == Item::None);
        UNSCOPED_INFO("not a string that only ends an argument");
        CHECK(kind("llPlaySound(prefix + \"x", false) == Item::None);
        UNSCOPED_INFO("not after a keyword");
        CHECK(kind("return \"x", true) == Item::None);
        UNSCOPED_INFO("not in a comment");
        CHECK(kind("// llPlaySound(\"x", false) == Item::None);

        std::string callee;
        S32         argument = -1;
        ALCodeEditor& e = editor("llSetLinkAlpha(2, ", false);
        {
            INFO("the call around a place");
            CHECK(Words::callAt(e, ALTextPos(0, 18), callee, argument));
            CHECK(callee == "llSetLinkAlpha");
            CHECK(argument == 1);
        }
    }

    TEST_CASE_METHOD(alscriptstudiowords_data, "alscriptstudiowords: SLua handler parameters keep their types' own commas and variadics", "[alscriptstudiowords]")
    {
        slua["events"]["mapped"] = LLSD().with("arguments", LLSD().with(0, argument("pairs", "Map<K, V>")).with(1, argument("n", "number")));
        slua["events"]["called"] =
            LLSD().with("arguments", LLSD().with(0, argument("back", "(number) -> ()")).with(1, argument("data", "{[string]: any}")));
        slua["events"]["varied"] = LLSD().with("arguments", LLSD().with(0, argument("first", "number")).with(1, argument("...", "any")));
        slua["events"]["bare"]   = LLSD().with("arguments", LLSD().with(0, argument("...any", "")));
        Words::forget();
        const auto   handler = [](const Vocab& word, bool typed) { return Words::completionFor(word, true, typed).snippet; };
        const Vocab& mapped  = known(true, "mapped");
        UNSCOPED_INFO("a generic's comma");
        CHECK(handler(mapped, true) == std::string("function LLEvents.mapped(pairs: Map<K, V>, n: number)\n    $0\nend"));
        UNSCOPED_INFO("and untyped");
        CHECK(handler(mapped, false) == std::string("function LLEvents.mapped(pairs, n)\n    $0\nend"));
        const Vocab& called = known(true, "called");
        UNSCOPED_INFO("a function type's arrow, a table type's colon");
        CHECK(handler(called, true) ==
              std::string("function LLEvents.called(back: (number) -> (), data: {[string]: any})\n    $0\nend"));
        const Vocab& varied = known(true, "varied");
        UNSCOPED_INFO("a variadic typed");
        CHECK(handler(varied, true) == std::string("function LLEvents.varied(first: number, ...: any)\n    $0\nend"));
        UNSCOPED_INFO("and not");
        CHECK(handler(varied, false) == std::string("function LLEvents.varied(first, ...)\n    $0\nend"));
        const Vocab& bare = known(true, "bare");
        UNSCOPED_INFO("one written ...any");
        CHECK(handler(bare, true) == std::string("function LLEvents.bare(...: any)\n    $0\nend"));
        UNSCOPED_INFO("and untyped");
        CHECK(handler(bare, false) == std::string("function LLEvents.bare(...)\n    $0\nend"));
    }
}
