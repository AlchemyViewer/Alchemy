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

#include "../test/lltut.h"

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
        keywords["events"]["touch_start"] = LLSD().with("arguments", LLSD().with(0, argument("num_detected", "number")));
        return keywords;
    }
}

namespace tut
{
    struct alscriptstudiowords_data
    {
        al_studio_test::StudioWindow window;
        std::vector<std::string>     extra;
        LLSD                         lsl  = lslKeywords();
        LLSD                         slua = sluaKeywords();

        alscriptstudiowords_data()
        {
            Words::Sources& sources   = Words::sources();
            sources.keywords          = [this](bool lua) { return lua ? slua : lsl; };
            sources.preprocessorWords = [this] { return extra; };
            sources.lslHelpUrl        = [] { return std::string("https://wiki.example/[LSL_STRING]"); };
            Words::forget();
        }
        ~alscriptstudiowords_data()
        {
            Words::sources() = Words::Sources();
            Words::forget();
        }
        ALCodeEditor& editor(const std::string& text, bool lua)
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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

    typedef test_group<alscriptstudiowords_data> alscriptstudiowords_group;
    typedef alscriptstudiowords_group::object    alscriptstudiowords_object;
    alscriptstudiowords_group                    alscriptstudiowords_instance("alscriptstudiowords");

    template<> template<>
    void alscriptstudiowords_object::test<1>()
    {
        set_test_name("the vocabulary from the definitions: each kind's detail, the deprecated marked, sorted, a group it does not know left out");
        const std::vector<Vocab>& words = Words::vocabulary(false);
        ensure_equals("six", words.size(), size_t(6));
        ensure("sorted", std::is_sorted(words.begin(), words.end(), [](const Vocab& a, const Vocab& b) { return a.text < b.text; }));
        const Vocab* say = Words::word(false, "llSay");
        ensure("a function's call, with its arguments' types", say && say->kind == ALSyntaxKind::Function &&
                                                                  say->detail == "llSay(integer channel, string msg)");
        ensure("and what returns", Words::word(false, "llOld")->detail == "integer llOld()" && Words::word(false, "llOld")->deprecated);
        ensure_equals("an event's", Words::word(false, "touch_start")->detail, std::string("touch_start(integer num_detected)"));
        ensure_equals("a constant's type and value", Words::word(false, "PI")->detail, std::string("float PI = 3.14159265"));
        ensure_equals("a type's tooltip's first line", Words::word(false, "integer")->detail, std::string("A whole number."));
        ensure("a control", Words::word(false, "jump")->kind == ALSyntaxKind::Control);
        ensure("not a word", !Words::word(false, "x") && !Words::word(false, "") && !Words::word(false, "nothing"));
        ensure_equals("SLua's () returns nothing worth saying", Words::word(true, "ll.Say")->detail, std::string("ll.Say(number channel)"));
    }

    template<> template<>
    void alscriptstudiowords_object::test<2>()
    {
        set_test_name("the vocabulary kept until forgotten, then built again from the definitions as they are, the lookup with it");
        ensure("there", Words::word(false, "llSay") != nullptr);
        lsl["functions"].erase("llSay");
        lsl["functions"]["llShout"] = LLSD().with("return", "");
        ensure("kept", Words::word(false, "llSay") != nullptr && !Words::word(false, "llShout"));
        Words::forget();
        ensure("built again", !Words::word(false, "llSay") && Words::word(false, "llShout") != nullptr);
        Words::sources().keywords = [](bool) { return LLSD("not a map"); };
        Words::forget();
        ensure("definitions that are not: no words", Words::vocabulary(false).empty() && !Words::word(false, "llShout"));
    }

    template<> template<>
    void alscriptstudiowords_object::test<3>()
    {
        set_test_name("an editor taught the words: each kind's table, the deprecated apart, and LSL's preprocessor words while they are on");
        ALCodeEditor& e = editor("default {}", false);
        extra           = { "switch", "case" };
        Words::teach(e, false);
        const ALSyntaxWords& tables = e.highlighter().words();
        ensure("functions", tables.has("function", "llSay") && !tables.has("function", "llOld"));
        ensure("the deprecated", tables.has("deprecated", "llOld"));
        ensure("events, constants, types, controls", tables.has("event", "touch_start") && tables.has("constant", "PI") &&
                                                         tables.has("type", "integer") && tables.has("control", "jump"));
        ensure("the preprocessor's", tables.has("control", "switch") && tables.has("control", "case"));
        ALCodeEditor& s = editor("x", true);
        Words::teach(s, true);
        ensure("SLua's own, and none of the preprocessor's", s.highlighter().words().has("function", "ll.Say") &&
                                                                 !s.highlighter().words().has("control", "switch"));
    }

    template<> template<>
    void alscriptstudiowords_object::test<4>()
    {
        set_test_name("straight inside a state or not, braces in strings and comments passed over");
        ALCodeEditor& e = editor("default\n{\n    \n    touch_start(integer n)\n    {\n        \n    }\n}\n"
                                 "state /* named */ two { \"{\" // {\n  \n}\n",
                                 false);
        ensure("in default", Words::inStateBody(e, ALTextPos(2, 4)));
        ensure("in a handler: not", !Words::inStateBody(e, ALTextPos(5, 8)));
        ensure("after default: not", !Words::inStateBody(e, ALTextPos(8, 0)));
        ensure("in a state named, a comment between, past a brace in a string and a comment", Words::inStateBody(e, ALTextPos(9, 2)));
    }

    template<> template<>
    void alscriptstudiowords_object::test<5>()
    {
        set_test_name("completions: a word, an event as a handler to fill in in either language; a handler only where one goes; snippets");
        const ALCodeEditor::Completion lsl = Words::completionFor(*Words::word(false, "touch_start"), false);
        ensure_equals("LSL's handler", lsl.snippet, std::string("touch_start(integer num_detected)\n{\n    $0\n}"));
        const ALCodeEditor::Completion slua = Words::completionFor(*Words::word(true, "touch_start"), true);
        ensure_equals("SLua's, set on LLEvents", slua.snippet, std::string("LLEvents.touch_start = function(num_detected)\n    $0\nend"));
        const ALCodeEditor::Completion say = Words::completionFor(*Words::word(false, "llSay"), false);
        ensure("a function as itself", say.snippet.empty() && say.text == "llSay" && say.documentation && *say.documentation == "Says msg on channel.\nMore.");

        ALCodeEditor&                         e = editor("default\n{\n    t\n}\nt\n", false);
        ALScriptSnippets::Snippet             snippet;
        snippet.name   = "Timer";
        snippet.prefix = "timer";
        snippet.body   = "llSetTimerEvent($1);";
        std::vector<ALCodeEditor::Completion> out;
        Words::complete(false, e, ALTextPos(2, 5), "t", { snippet }, "Snippet", out);
        ensure("inside the state: the handler and the snippet", texts(out) == std::vector<std::string>{ "touch_start", "timer" });
        ensure_equals("the snippet said so", out[1].detail, std::string("Snippet  Timer"));
        out.clear();
        Words::complete(false, e, ALTextPos(4, 1), "t", { snippet }, "Snippet", out);
        ensure("outside: no handler", texts(out) == std::vector<std::string>{ "timer" });
        out.clear();
        ALScriptSnippets::Snippet member = snippet;
        member.prefix                    = "ll.Snip";
        Words::complete(true, e, ALTextPos(4, 1), "ll.S", { member }, "Snippet", out);
        ensure("a member: the vocabulary's alone", texts(out) == std::vector<std::string>{ "ll.Say" });
    }

    template<> template<>
    void alscriptstudiowords_object::test<6>()
    {
        set_test_name("the hover card's words: a member asked as the whole name, a word as itself, the deprecated said; a word of the script's none");
        ALCodeEditor& s = editor("ll.Say(0)\nx = math.pi\n", true);
        std::string   text;
        ensure("ll.Say under Say", Words::hoverText(true, s.document(), ALTextPos(0, 4), "Say", text) && text == "ll.Say(number channel)");
        ensure("math.pi under pi", Words::hoverText(true, s.document(), ALTextPos(1, 10), "pi", text) && text == "number math.pi()");
        ensure("a word of the script's", !Words::hoverText(true, s.document(), ALTextPos(1, 0), "x", text));
        ALCodeEditor& e = editor("llOld()\n", false);
        ensure("the deprecated", Words::hoverText(false, e.document(), ALTextPos(0, 2), "llOld", text) &&
                                     text == "integer llOld()\nGone.\n" + ALCodeEditor::deprecatedNote());
    }

    template<> template<>
    void alscriptstudiowords_object::test<7>()
    {
        set_test_name("the Insert menu's list and the reference: a word's page on the wiki, SLua's ll names too, the libraries', the portal");
        ALScriptSnippets::Snippet snippet;
        snippet.name   = "Timer";
        snippet.prefix = "timer";
        snippet.detail = "Every so often";
        const std::vector<ALQuickOpen::Candidate> snippets = Words::library(false, "snippet", { snippet }, "Deprecated");
        ensure("a snippet, by what it is", snippets.size() == 1 && snippets[0].value == "Timer\ntimer" && snippets[0].also == "timer");
        const std::vector<ALQuickOpen::Candidate> functions = Words::library(false, "function", {}, "Deprecated");
        ensure("the functions alone", functions.size() == 2 && functions[0].label == "llOld" && functions[1].label == "llSay");
        ensure("the deprecated said so, the rest by their tooltip's first line",
               functions[0].detail == "Deprecated" && functions[1].detail == "Says msg on channel.");
        ensure("the events", Words::library(false, "event", {}, "") .size() == 1);

        ensure_equals("an LSL name", Words::helpUrl(false, "llSay"), std::string("https://wiki.example/llSay"));
        ensure_equals("SLua's ll name, as LSL's", Words::helpUrl(true, "ll.Say"), std::string("https://wiki.example/llSay"));
        ensure_equals("a library's", Words::helpUrl(true, "math.pi"), std::string("https://luau.org/library"));
        ensure_equals("the rest", Words::helpUrl(true, "LLEvents"), std::string("https://wiki.secondlife.com/wiki/Lua_Alpha"));
        ensure_equals("nothing: the portal", Words::helpUrl(false, ""), std::string("https://wiki.example/LSL_Portal"));
        ensure_equals("the reference's words", Words::referenceText(*Words::word(false, "PI"), false),
                      std::string("float PI = 3.14159265\n\nThe ratio.\nhttps://wiki.example/PI"));
    }

    template<> template<>
    void alscriptstudiowords_object::test<8>()
    {
        set_test_name("the marks: a tab's by what it is, a symbol's by its kind");
        ALScriptStudioDoc doc;
        ensure_equals("an LSL script", std::string(Words::imageNameOf(doc)), std::string("Inv_Script"));
        doc.language.lua = true;
        ensure_equals("an SLua one", std::string(Words::imageNameOf(doc)), std::string("Inv_Script_Luau"));
        doc.notecard = true;
        ensure_equals("a notecard", std::string(Words::imageNameOf(doc)), std::string("Inv_Notecard"));
        doc.file = "/a/notes.txt";
        ensure_equals("a file", std::string(Words::imageNameOf(doc)), std::string("Studio_File"));
        doc.name = ".luaurc";
        ensure_equals("a config", std::string(Words::imageNameOf(doc)), std::string("Studio_Config"));
        ensure_equals("a function", std::string(Words::imageNameOf(ALScriptSymbolKind::Function)), std::string("Symbol_Function"));
        ensure_equals("a state as a label", std::string(Words::imageNameOf(ALScriptSymbolKind::State)), std::string("Symbol_Label"));
    }

    template<> template<>
    void alscriptstudiowords_object::test<9>()
    {
        set_test_name("each kind of symbol called by one of the window's words, a word of its own");
        if (!window.floater)
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        std::set<std::string> said;
        for (S32 kind = static_cast<S32>(ALScriptSymbolKind::Keyword); kind <= static_cast<S32>(ALScriptSymbolKind::Module); ++kind)
        {
            const char* word = Words::kindWordOf(static_cast<ALScriptSymbolKind>(kind));
            ensure("a word for each", word != nullptr && window.floater->hasString(word));
            said.insert(window.floater->getString(word));
        }
        ensure_equals("each its own", said.size(), size_t(static_cast<S32>(ALScriptSymbolKind::Module) + 1));
        ensure_equals("a function", window.floater->getString(Words::kindWordOf(ALScriptSymbolKind::Function)), std::string("function"));
    }

    template<> template<>
    void alscriptstudiowords_object::test<10>()
    {
        set_test_name("what the definitions say of a builtin beyond its declaration: its words, its forced delay, that only a god may call it; a member is no global of its name");
        lsl["functions"]["llEmail"]            = LLSD().with("return", "").with("sleep", "20.0").with("tooltip", "Sends an email.");
        lsl["functions"]["llGodLikeRezObject"] = LLSD().with("return", "").with("god-mode", true).with("sleep", "0.0");
        slua["functions"]["type"]              = LLSD().with("return", "string").with("tooltip", "What a value is.");
        Words::forget();
        const Vocab* email = Words::word(false, "llEmail");
        ensure("the email", email && email->sleep == "20.0" && !email->godMode);
        const std::string notes = Words::notesOf(*email);
        ensure("its words and its delay: " + notes, notes.find("Sends an email.") == 0 && notes.find("20.0") != std::string::npos);
        const Vocab* god = Words::word(false, "llGodLikeRezObject");
        ensure("a god's, no delay said for none", god && god->godMode && god->sleep.empty());
        ensure("said", !Words::notesOf(*god).empty());
        // `type` under the mouse in `obj.type` is some table's field.
        ALCodeEditor& s = editor("local t = obj.type\nlocal u = type(t)\n", true);
        std::string   text;
        ensure("a member: not the global", !Words::hoverText(true, s.document(), ALTextPos(0, 15), "type", text));
        ensure("the global itself", Words::hoverText(true, s.document(), ALTextPos(1, 11), "type", text) && text.find("What a value is.") != std::string::npos);
    }

    template<> template<>
    void alscriptstudiowords_object::test<11>()
    {
        set_test_name("a function's link-number argument, as the definitions name it, in LSL and in SLua");
        lsl["functions"]["llSetLinkAlpha"] = LLSD().with("return", "").with(
            "arguments",
            LLSD().with(0, argument("LinkNumber", "integer")).with(1, argument("Opacity", "float")).with(2, argument("Face", "integer")));
        slua["functions"]["ll.MessageLinked"] =
            LLSD().with("return", "()").with("arguments", LLSD().with(0, argument("link", "number")).with(1, argument("num", "number")));
        Words::forget();
        ensure("llSetLinkAlpha's first", Words::linkArgument(false, "llSetLinkAlpha", 0));
        ensure("not its second", !Words::linkArgument(false, "llSetLinkAlpha", 1));
        ensure("nor past its last", !Words::linkArgument(false, "llSetLinkAlpha", 3));
        ensure("llSay has none", !Words::linkArgument(false, "llSay", 0));
        ensure("a word not known, none", !Words::linkArgument(false, "llNothing", 0));
        ensure("SLua's by its name with its library", Words::linkArgument(true, "ll.MessageLinked", 0) && !Words::linkArgument(true, "ll.MessageLinked", 1));
    }
    template<> template<>
    void alscriptstudiowords_object::test<12>()
    {
        set_test_name("a language's tables are made once and shared by every editor of it; new definitions are read once however many ask; new preprocessor words make new tables");
        S32         asked   = 0;
        std::string version = "one";
        Words::sources().keywords = [this, &asked](bool lua) {
            ++asked;
            return lua ? slua : lsl;
        };
        Words::sources().definitionsVersion = [&version] { return version; };
        Words::forget();
        ALCodeEditor& a = editor("default {}", false);
        ALCodeEditor& b = editor("default {}", false);
        ALCodeEditor& s = editor("x", true);
        Words::teach(a, false);
        Words::teach(b, false);
        Words::teach(s, true);
        ensure("one set for both LSL editors", &a.highlighter().words() == &b.highlighter().words());
        ensure("another for SLua's", &s.highlighter().words() != &a.highlighter().words());
        const S32 first = asked;
        Words::teach(a, false);
        ensure_equals("taught again from what is kept", asked, first);

        version                     = "two";
        lsl["functions"]["llShout"] = LLSD().with("return", "");
        Words::teach(a, false);
        Words::teach(b, false);
        ensure_equals("new definitions read once for both", asked, first + 1);
        ensure("and shared still, with the new word", &a.highlighter().words() == &b.highlighter().words() &&
                                                          a.highlighter().words().has("function", "llShout"));

        const ALSyntaxWords* before = &a.highlighter().words();
        extra                       = { "switch" };
        Words::teach(a, false);
        ensure("new preprocessor words, new tables", &a.highlighter().words() != before && a.highlighter().words().has("control", "switch"));
    }
}
