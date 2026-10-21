/**
 * @file alfoldmodel_test.cpp
 * @brief The blocks of a code editor's text that fold, and which of them are folded, with nothing laid out.
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

#include "alfoldmodel.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

namespace
{
    struct alfoldmodel_data
    {
        ALFoldModel model;

        // Each block as start-end, in order, by a model of the document's
        // own.
        static std::string blocks(const ALTextDocument& doc, S32 tab_width = 4)
        {
            ALFoldModel own;
            std::string out;
            for (const ALFoldModel::Region& region : own.regions(doc, tab_width))
            {
                out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
            }
            return out;
        }
        static std::string lines(const std::vector<std::pair<S32, S32>>& hidden)
        {
            std::string out;
            for (const auto& [first, last] : hidden)
            {
                out += (out.empty() ? "" : " ") + std::to_string(first) + "-" + std::to_string(last);
            }
            return out;
        }
    };

    TEST_CASE_METHOD(alfoldmodel_data, "alfoldmodel: an indent block takes its closer and blank lines and a lone brace", "[alfoldmodel]")
    {
        const ALTextDocument lua("function f()\n    if x then\n        y()\n\n    end\nend\nz()");
        UNSCOPED_INFO("each block, its closer taken");
        CHECK(blocks(lua) == std::string("0-5 1-4"));

        const ALTextDocument lsl("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}");
        UNSCOPED_INFO("a brace on a line of its own folds from the header above it");
        CHECK(blocks(lsl) == std::string("0-6 2-5"));

        // Three spaces are deeper than a tab of two, and not one of four.
        const ALTextDocument tabs("a\n\tb\n   c");
        UNSCOPED_INFO("a tab as wide as it is set");
        CHECK(blocks(tabs, 4) == std::string("0-2"));
        UNSCOPED_INFO("and narrower");
        CHECK(blocks(tabs, 2) == std::string("0-2 1-2"));

        {
            INFO("the block starting at a line");
            REQUIRE(model.startingAt(lua, 4, 1));
            CHECK(model.startingAt(lua, 4, 1)->end == 4);
        }
        UNSCOPED_INFO("none starts at a line inside");
        CHECK_FALSE(model.startingAt(lua, 4, 2));
        {
            INFO("the innermost around it");
            REQUIRE(model.around(lua, 4, 2));
            CHECK(model.around(lua, 4, 2)->start == 1);
        }
        {
            INFO("not one that starts on it");
            REQUIRE(model.around(lua, 4, 1));
            CHECK(model.around(lua, 4, 1)->start == 0);
        }
        UNSCOPED_INFO("none around a line outside every block");
        CHECK_FALSE(model.around(lua, 4, 6));
    }

    TEST_CASE_METHOD(alfoldmodel_data, "alfoldmodel: fold and unfold by a line or all at once and reveal a line inside", "[alfoldmodel]")
    {
        const ALTextDocument doc("function f()\n    if x then\n        y()\n    end\nend");
        std::optional<ALFoldModel::Region> folded = model.fold(doc, 4, 2);
        {
            INFO("the innermost block around the line");
            REQUIRE(folded);
            CHECK(folded->start == 1);
            CHECK(folded->end == 3);
        }
        CHECK(model.isFolded(1));
        CHECK_FALSE(model.isFolded(0));
        UNSCOPED_INFO("not twice");
        CHECK_FALSE(model.fold(doc, 4, 1));
        UNSCOPED_INFO("what it hides");
        CHECK(lines(model.hidden(doc, 4)) == std::string("2-3"));
        {
            INFO("opened by a line inside it");
            CHECK(model.unfold(doc, 4, 2));
            CHECK_FALSE(model.isFolded(1));
        }
        UNSCOPED_INFO("nothing to open");
        CHECK_FALSE(model.unfold(doc, 4, 2));

        model.foldAll(doc, 4);
        CHECK(model.folded() == std::vector<S32>{ 0, 1 });
        {
            INFO("opened for a line inside two");
            CHECK(model.reveal(doc, 4, 2));
            CHECK(model.folded().empty());
        }
        model.fold(doc, 4, 1);
        {
            INFO("a line inside none opens nothing");
            CHECK_FALSE(model.reveal(doc, 4, 4));
            CHECK(model.isFolded(1));
        }
        model.unfoldAll();
        CHECK(model.folded().empty());
    }

    TEST_CASE_METHOD(alfoldmodel_data, "alfoldmodel: folds follow edits around them and go with their blocks", "[alfoldmodel]")
    {
        ALTextDocument doc("a\nf()\n    x\n    y\ng()\n    z");
        model.fold(doc, 4, 1);
        model.fold(doc, 4, 4);
        CHECK(model.folded() == std::vector<S32>{ 1, 4 });

        // Two lines put in above: both move down.
        ALTextDocument::Edit edit = doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)), "\nb\nc");
        model.edited(edit);
        CHECK(model.folded() == std::vector<S32>{ 3, 6 });
        UNSCOPED_INFO("hiding what they did");
        CHECK(lines(model.hidden(doc, 4)) == std::string("4-5 7-7"));

        // Typing on a block's first line is not opening it.
        edit = doc.replace(ALTextRange(ALTextPos(3, 3), ALTextPos(3, 3)), " -- note");
        model.edited(edit);
        CHECK(model.isFolded(3));

        // Whole lines taken from above a folded block: it moves up with them.
        edit = doc.replace(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)), "");
        model.edited(edit);
        CHECK(model.folded() == std::vector<S32>{ 1, 4 });

        // An edit through a folded block's first line takes the fold.
        edit = doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 3)), "");
        model.edited(edit);
        CHECK(model.folded() == std::vector<S32>{ 3 });

        // Its block gone -- the line under it outdented -- the fold goes.
        CHECK(doc.text() == std::string("a -- note\n    x\n    y\ng()\n    z"));
        edit = doc.replace(ALTextRange(ALTextPos(4, 0), ALTextPos(4, 4)), "");
        model.edited(edit);
        UNSCOPED_INFO("folded still");
        CHECK(model.isFolded(3));
        {
            INFO("nothing hidden once its block is gone");
            CHECK(model.hidden(doc, 4).empty());
            CHECK(model.folded().empty());
        }
    }

    TEST_CASE_METHOD(alfoldmodel_data, "alfoldmodel: syntax blocks span lines with lone braces and middles and regions", "[alfoldmodel]")
    {
        const ALTextDocument doc("default\n{\n    if (a) {\n        x;\n    } else {\n        y;\n    }\n}\n// #region setup\nz;\n// #endregion\n");
        ALFoldModel folds;
        // Braces as the syntax, a word `else` a middle where it stands alone.
        folds.setSyntax(
            [&doc](S32 line, std::vector<ALFoldModel::Block>& out) {
                const std::string& text = doc.line(line);
                for (S32 i = 0; i < static_cast<S32>(text.size()); ++i)
                {
                    if (text[static_cast<size_t>(i)] == '{')
                    {
                        out.push_back({ i, ALFoldModel::Event::Open });
                    }
                    else if (text[static_cast<size_t>(i)] == '}')
                    {
                        out.push_back({ i, ALFoldModel::Event::Close });
                    }
                }
            },
            nullptr);
        folds.setLineComment("//");
        std::string out;
        for (const ALFoldModel::Region& region : folds.regions(doc, 4))
        {
            out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
        }
        UNSCOPED_INFO("the state from its header, the if to before its else, the else to its close, the region");
        CHECK(out == std::string("0-7 2-3 4-6 8-10"));
        UNSCOPED_INFO("a blank line's indent is the next's");
        CHECK(folds.indentOf(doc, 4, 3) == 8);
        const std::vector<S32> open = folds.openAt(doc, 4, 5, 3);
        {
            INFO("open at the else's line: the state, from its header, and the else");
            REQUIRE(open.size() == 2);
            CHECK(open[0] == 0);
            CHECK(open[1] == 4);
        }
        UNSCOPED_INFO("none at the region's line");
        CHECK(folds.openAt(doc, 4, 9, 3).empty());

        const ALTextDocument lua("if a then\n  x()\nelseif b then\n  y()\nelse\n  z()\nend\n");
        ALFoldModel words;
        words.setSyntax(
            [&lua](S32 line, std::vector<ALFoldModel::Block>& out) {
                const std::string& text = lua.line(line);
                if (text.rfind("if", 0) == 0)
                {
                    out.push_back({ 7, ALFoldModel::Event::Open });
                }
                else if (text.rfind("else", 0) == 0)
                {
                    out.push_back({ 0, ALFoldModel::Event::Middle });
                }
                else if (text == "end")
                {
                    out.push_back({ 0, ALFoldModel::Event::Close });
                }
            },
            nullptr);
        out.clear();
        for (const ALFoldModel::Region& region : words.regions(lua, 4))
        {
            out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
        }
        UNSCOPED_INFO("each arm a block of its own, the last through the end");
        CHECK(out == std::string("0-1 2-3 4-6"));
    }

    TEST_CASE_METHOD(alfoldmodel_data, "alfoldmodel: lines taken from a fold's start take the fold and Return moves it down", "[alfoldmodel]")
    {
        const char* const text = "foo()\n{\n    x();\n}\nbar()\n{\n    y();\n}";
        ALTextDocument    doc(text);
        CHECK(blocks(doc) == std::string("0-3 4-7"));
        model.fold(doc, 4, 0);
        ALTextDocument::Edit edit = doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(4, 0)), "");
        model.edited(edit);
        UNSCOPED_INFO("the block after them now first");
        CHECK(doc.line(0) == std::string("bar()"));
        UNSCOPED_INFO("the fold gone with its line, the block now there not folded for it");
        CHECK(model.folded().empty());

        // Both folded: the one after moves up, and is one fold still.
        ALTextDocument both(text);
        ALFoldModel    two;
        two.fold(both, 4, 0);
        two.fold(both, 4, 4);
        edit = both.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(4, 0)), "");
        two.edited(edit);
        UNSCOPED_INFO("the one after moved up, once");
        CHECK(two.folded() == std::vector<S32>{ 0 });
        UNSCOPED_INFO("hiding its lines once");
        CHECK(lines(two.hidden(both, 4)) == std::string("1-3"));

        // Return at its first line's start: the line goes down, and the fold
        // with it.
        ALTextDocument above(text);
        ALFoldModel    down;
        down.fold(above, 4, 0);
        edit = above.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "\n");
        down.edited(edit);
        UNSCOPED_INFO("moved down with its line");
        CHECK(down.folded() == std::vector<S32>{ 1 });
        UNSCOPED_INFO("hiding what it did");
        CHECK(lines(down.hidden(above, 4)) == std::string("2-4"));
        edit = above.replace(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)), "q");
        down.edited(edit);
        UNSCOPED_INFO("typing at its start keeps it");
        CHECK(down.folded() == std::vector<S32>{ 1 });
    }

    TEST_CASE_METHOD(alfoldmodel_data, "alfoldmodel: openAt gives a lone bracket under an opening header its own block once", "[alfoldmodel]")
    {
        const ALTextDocument doc("llSetLinkPrimitiveParamsFast(LINK_THIS,\n[\n    PRIM_COLOR, ALL_SIDES\n]);\n");
        ALFoldModel          folds;
        folds.setSyntax(
            [&doc](S32 line, std::vector<ALFoldModel::Block>& out) {
                const std::string& text = doc.line(line);
                for (S32 i = 0; i < static_cast<S32>(text.size()); ++i)
                {
                    const char c = text[static_cast<size_t>(i)];
                    if (c == '(' || c == '[')
                    {
                        out.push_back({ i, ALFoldModel::Event::Open });
                    }
                    else if (c == ')' || c == ']')
                    {
                        out.push_back({ i, ALFoldModel::Event::Close });
                    }
                }
            },
            nullptr);
        std::string out;
        for (const ALFoldModel::Region& region : folds.regions(doc, 4))
        {
            out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
        }
        UNSCOPED_INFO("the call's block from its line, the list's from its own");
        CHECK(out == std::string("0-3 1-3"));
        UNSCOPED_INFO("both open inside them, each once");
        CHECK(folds.openAt(doc, 4, 2, 8) == std::vector<S32>({ 0, 1 }));
        UNSCOPED_INFO("the innermost alone the list's");
        CHECK(folds.openAt(doc, 4, 2, 1) == std::vector<S32>({ 1 }));
    }

    TEST_CASE_METHOD(alfoldmodel_data, "alfoldmodel: a word that begins its line opens a block there, not under the end above", "[alfoldmodel]")
    {
        const ALTextDocument doc("local function a()\nend\n\nfunction LLEvents.touch_start(n)\n    x()\nend\nrepeat\n    y()\nuntil z\n");
        ALFoldModel          folds;
        // As SLua's grammar has them: `function` and `repeat` open, `end`
        // and `until` close, and the brackets.
        folds.setSyntax(
            [&doc](S32 line, std::vector<ALFoldModel::Block>& out) {
                const std::string& text = doc.line(line);
                for (const char* word : { "function", "repeat", "end", "until" })
                {
                    const size_t at = text.find(word);
                    if (at != std::string::npos)
                    {
                        const bool opens = word[0] == 'f' || word[0] == 'r';
                        out.push_back({ static_cast<S32>(at), opens ? ALFoldModel::Event::Open : ALFoldModel::Event::Close });
                    }
                }
                for (S32 i = 0; i < static_cast<S32>(text.size()); ++i)
                {
                    const char c = text[static_cast<size_t>(i)];
                    if (c == '(' || c == ')')
                    {
                        out.push_back({ i, c == '(' ? ALFoldModel::Event::Open : ALFoldModel::Event::Close });
                    }
                }
                std::stable_sort(out.begin(), out.end(), [](const ALFoldModel::Block& l, const ALFoldModel::Block& r) { return l.column < r.column; });
            },
            nullptr);
        std::string out;
        for (const ALFoldModel::Region& region : folds.regions(doc, 4))
        {
            out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
        }
        UNSCOPED_INFO("each from its own line: the handler's not from the end above it, the repeat's not from that end");
        CHECK(out == std::string("0-1 3-5 6-8"));
        UNSCOPED_INFO("the handler's line pinned over its body");
        CHECK(folds.openAt(doc, 4, 4, 3) == std::vector<S32>({ 3 }));
        UNSCOPED_INFO("the repeat's over its");
        CHECK(folds.openAt(doc, 4, 7, 3) == std::vector<S32>({ 6 }));
    }

    TEST_CASE_METHOD(alfoldmodel_data, "alfoldmodel: syntax blocks are read once and again only for lines relexed", "[alfoldmodel]")
    {
        const ALTextDocument doc("a {\n  b {\n  }\n}\nc {\n}\n");
        ALFoldModel          folds;
        // The lines whose braces lex as something other than code, as in a
        // comment; and how often blocks were asked for, and through which
        // line the text was lexed.
        std::vector<bool> quiet(static_cast<size_t>(doc.lineCount()), false);
        S32               asked   = 0;
        S32               lexed   = -1;
        folds.setSyntax(
            [&](S32 line, std::vector<ALFoldModel::Block>& out) {
                ++asked;
                const std::string& text = doc.line(line);
                for (S32 i = 0; !quiet[static_cast<size_t>(line)] && i < static_cast<S32>(text.size()); ++i)
                {
                    if (text[static_cast<size_t>(i)] == '{' || text[static_cast<size_t>(i)] == '}')
                    {
                        out.push_back({ i, text[static_cast<size_t>(i)] == '{' ? ALFoldModel::Event::Open : ALFoldModel::Event::Close });
                    }
                }
            },
            [&](S32 line) { lexed = line; });
        const auto found = [&]() {
            std::string out;
            for (const ALFoldModel::Region& region : folds.regions(doc, 4))
            {
                out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
            }
            return out;
        };
        CHECK(found() == std::string("0-3 1-2 4-5"));
        UNSCOPED_INFO("each line with anything on it asked once");
        CHECK(asked == 6);
        UNSCOPED_INFO("the text lexed through its last line first");
        CHECK(lexed == doc.lineCount() - 1);
        folds.invalidate();
        quiet[1] = quiet[2] = true;
        UNSCOPED_INFO("found again, none asked again");
        CHECK(found() == std::string("0-3 1-2 4-5"));
        UNSCOPED_INFO("asked no more");
        CHECK(asked == 6);
        folds.relexed(1, 2);
        UNSCOPED_INFO("those said lexed anew read again");
        CHECK(found() == std::string("0-3 4-5"));
        UNSCOPED_INFO("and they alone");
        CHECK(asked == 8);
        {
            INFO("the sticky headers lex through the line before theirs");
            CHECK(folds.openAt(doc, 4, 5, 3) == std::vector<S32>({ 4 }));
            CHECK(lexed == 4);
        }
    }

    TEST_CASE_METHOD(alfoldmodel_data, "alfoldmodel: blocks kept through random edits match a whole walk and walk only near", "[alfoldmodel]")
    {
        // The same edits on every run from a seed, four runs.
        S32 nearby  = 0;
        S32 spliced = 0;
        // Functions with braces their own lines' and their headers', an else,
        // regions, and block comments, which a lexer here reads as a
        // highlighter does: a line's braces count where they are code. Or
        // many more of them, every third brace alone on its line left out,
        // so that what is open grows down the text.
        const auto functions = [](S32 count, bool unclosed) {
            std::string text;
            for (S32 f = 0; f < count; ++f)
            {
                text += f % 3 == 0 ? "f()\n{\n    if (a) {\n        b();\n    } else {\n        c();\n    }\n}\n"
                      : f % 3 == 1 ? "// #region r\ng() {\n    d();\n}\n// #endregion\n\n"
                                   : "/* h() {\n */\nh()\n{\n    e();\n}\n";
            }
            if (!unclosed)
            {
                return text;
            }
            std::string out;
            S32         alone = 0;
            size_t      at    = 0;
            while (at < text.size())
            {
                const size_t end  = text.find('\n', at);
                std::string  line = text.substr(at, end - at);
                if (line == "}" && ++alone % 3 == 0)
                {
                    line.clear();
                }
                out += line + "\n";
                at = end + 1;
            }
            return out;
        };
        // `sweeping`: now and then hundreds of lines taken at once, over
        // places the walk keeps.
        const auto run = [&](U32 first_seed, const std::string& text, bool sweeping) {
            ALTextDocument doc(text);
            // Whether each line starts inside a comment, as the lexer has it.
            const auto starts = [&doc]() {
                std::vector<bool> out(static_cast<size_t>(doc.lineCount()), false);
                bool              inside = false;
                for (S32 l = 0; l < doc.lineCount(); ++l)
                {
                    out[static_cast<size_t>(l)] = inside;
                    const std::string& line     = doc.line(l);
                    for (size_t i = 0; i + 1 < line.size(); ++i)
                    {
                        if (!inside && line[i] == '/' && line[i + 1] == '*')
                        {
                            inside = true;
                            ++i;
                        }
                        else if (inside && line[i] == '*' && line[i + 1] == '/')
                        {
                            inside = false;
                            ++i;
                        }
                    }
                }
                return out;
            };
            std::vector<bool> lexed = starts();
            const auto        blocks = [&doc, &lexed](S32 line, std::vector<ALFoldModel::Block>& out) {
                const std::string& t      = doc.line(line);
                bool               inside = lexed[static_cast<size_t>(line)];
                for (S32 i = 0; i < static_cast<S32>(t.size()); ++i)
                {
                    const char c = t[static_cast<size_t>(i)];
                    const char n = i + 1 < static_cast<S32>(t.size()) ? t[static_cast<size_t>(i + 1)] : '\0';
                    if (!inside && c == '/' && n == '*')
                    {
                        inside = true;
                        ++i;
                    }
                    else if (inside && c == '*' && n == '/')
                    {
                        inside = false;
                        ++i;
                    }
                    else if (!inside && (c == '{' || c == '}'))
                    {
                        out.push_back({ i, c == '{' ? ALFoldModel::Event::Open : ALFoldModel::Event::Close });
                    }
                }
            };
            const auto said = [](const std::vector<ALFoldModel::Region>& regions) {
                std::string out;
                for (const ALFoldModel::Region& region : regions)
                {
                    out += (out.empty() ? "" : " ") + std::to_string(region.start) + "-" + std::to_string(region.end);
                }
                return out;
            };
            ALFoldModel kept;
            kept.setSyntax(blocks, nullptr);
            kept.setLineComment("//");
            UNSCOPED_INFO("all of it walked first");
            REQUIRE((kept.regions(doc, 4), kept.lastWalked()) == doc.lineCount());
            // The same edits on every run, each number drawn in a statement of
            // its own so that no compiler's order of arguments changes them.
            U32        seed = first_seed;
            const auto pick = [&seed](S32 n) {
                seed = seed * 1664525u + 1013904223u;
                return n > 0 ? static_cast<S32>((seed >> 8) % static_cast<U32>(n)) : 0;
            };
            const auto somewhere = [&]() {
                const S32 line   = pick(doc.lineCount());
                const S32 column = pick(doc.lineLength(line) + 1);
                return ALTextPos(line, column);
            };
            const char* const PIECES[] = { "", "x", "\n", "\n\n", "{", "}", "{\n", "}\n", "} else {", "/*", "*/", "// #region\n", "// #endregion\n",
                                           "k()\n{\n    m();\n}\n" };
            const S32         PIECE_COUNT = static_cast<S32>(sizeof(PIECES) / sizeof(PIECES[0]));
            for (S32 step = 0; step < 600; ++step)
            {
                const S32 how = pick(10);
                ALTextDocument::Edit edit;
                if (how < 7)
                {
                    // One run: a piece put in, over a stretch of up to a few
                    // lines or none.
                    const ALTextPos from  = somewhere();
                    S32             lines = pick(4);
                    if (sweeping)
                    {
                        const S32 big = pick(12);
                        lines         = big == 0 ? pick(400) : lines;
                    }
                    const S32       piece = pick(PIECE_COUNT);
                    const S32       to    = llmin(from.line + lines, doc.lineCount() - 1);
                    const S32       col   = pick(doc.lineLength(to) + 1);
                    const ALTextRange range = ALTextRange(from, ALTextPos(to, col)).normalised();
                    edit                    = doc.replace(how < 3 ? ALTextRange(from, from) : range, PIECES[piece]);
                }
                else
                {
                    // Several runs at once, as a Replace All makes them.
                    std::vector<std::pair<ALTextRange, std::string>> runs;
                    S32 line = pick(8);
                    while (line < doc.lineCount() && runs.size() < 6)
                    {
                        const S32 piece = pick(PIECE_COUNT);
                        runs.emplace_back(ALTextRange(ALTextPos(line, 0), ALTextPos(line, llmin(doc.lineLength(line), pick(3)))), PIECES[piece]);
                        const S32 gap = pick(40);
                        line += 2 + gap;
                    }
                    if (runs.empty())
                    {
                        continue;
                    }
                    edit = doc.replaceMany(std::move(runs));
                }
                // As the highlighter does: the lines after the edit lexed, and
                // those whose start the edit changed said to be lexed anew.
                const std::vector<bool> was = lexed;
                lexed                       = starts();
                kept.edited(edit, doc.lineCount());
                const std::vector<ALTextDocument::Edit::LineSpan>& spans = edit.lineSpans();
                S32 relexed_first = -1;
                S32 relexed_last  = -1;
                for (S32 l = 0; l < doc.lineCount(); ++l)
                {
                    // The line it was, where it was not one the edit made.
                    S32  old    = l;
                    bool inside = false;
                    S32  before = 0;
                    for (const ALTextDocument::Edit::LineSpan& span : spans)
                    {
                        const S32 made_first = span.first + before;
                        const S32 made_last  = made_first + llmax(span.made, 1) - 1;
                        if (l < made_first)
                        {
                            break;
                        }
                        if (l <= made_last)
                        {
                            inside = true;
                            break;
                        }
                        old    = l - span.shiftAfter;
                        before = span.shiftAfter;
                    }
                    if (!inside && old >= 0 && old < static_cast<S32>(was.size()) && was[static_cast<size_t>(old)] != lexed[static_cast<size_t>(l)])
                    {
                        relexed_first = relexed_first < 0 ? l : relexed_first;
                        relexed_last  = l;
                    }
                }
                if (relexed_first >= 0)
                {
                    kept.relexed(relexed_first, relexed_last);
                }
                ALFoldModel whole;
                whole.setSyntax(blocks, nullptr);
                whole.setLineComment("//");
                const std::string found = said(kept.regions(doc, 4));
                CAPTURE(first_seed, step);
                REQUIRE(found == said(whole.regions(doc, 4)));
                nearby += kept.lastWalked() < doc.lineCount() / 4 ? 1 : 0;
                spliced += kept.lastSpliced() ? 1 : 0;
            }
        };
        run(20261009u, functions(60, false), false);
        run(7u, functions(60, false), false);
        run(4242u, functions(60, false), false);
        run(99u, functions(400, true), true);
        UNSCOPED_INFO("most edits walked only near them: " << nearby);
        CHECK(nearby > 1200);
        UNSCOPED_INFO("and most put the blocks together from the walk's changes: " << spliced);
        CHECK(spliced > 300);
    }
}
