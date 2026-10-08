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

#include "../test/lltut.h"

#include <string>

namespace tut
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

    typedef test_group<alfoldmodel_data> alfoldmodel_group;
    typedef alfoldmodel_group::object    alfoldmodel_object;
    alfoldmodel_group                    alfoldmodel_instance("alfoldmodel");

    template<> template<>
    void alfoldmodel_object::test<1>()
    {
        set_test_name("a block is a line and the deeper ones after it, its closer with it, blank lines going with it, a brace alone its header's");
        const ALTextDocument lua("function f()\n    if x then\n        y()\n\n    end\nend\nz()");
        ensure_equals("each block, its closer taken", blocks(lua), std::string("0-5 1-4"));

        const ALTextDocument lsl("default\n{\n    state_entry()\n    {\n        llSay(0, \"hi\");\n    }\n}");
        ensure_equals("a brace on a line of its own folds from the header above it", blocks(lsl), std::string("0-6 2-5"));

        // Three spaces are deeper than a tab of two, and not one of four.
        const ALTextDocument tabs("a\n\tb\n   c");
        ensure_equals("a tab as wide as it is set", blocks(tabs, 4), std::string("0-2"));
        ensure_equals("and narrower", blocks(tabs, 2), std::string("0-2 1-2"));

        ensure("the block starting at a line", model.startingAt(lua, 4, 1) && model.startingAt(lua, 4, 1)->end == 4);
        ensure("none starts at a line inside", !model.startingAt(lua, 4, 2));
        ensure("the innermost around it", model.around(lua, 4, 2) && model.around(lua, 4, 2)->start == 1);
        ensure("not one that starts on it", model.around(lua, 4, 1) && model.around(lua, 4, 1)->start == 0);
        ensure("none around a line outside every block", !model.around(lua, 4, 6));
    }

    template<> template<>
    void alfoldmodel_object::test<2>()
    {
        set_test_name("folded and unfolded by a line, all at once, and opened for a line inside");
        const ALTextDocument doc("function f()\n    if x then\n        y()\n    end\nend");
        std::optional<ALFoldModel::Region> folded = model.fold(doc, 4, 2);
        ensure("the innermost block around the line", folded && folded->start == 1 && folded->end == 3);
        ensure("folded", model.isFolded(1) && !model.isFolded(0));
        ensure("not twice", !model.fold(doc, 4, 1));
        ensure_equals("what it hides", lines(model.hidden(doc, 4)), std::string("2-3"));
        ensure("opened by a line inside it", model.unfold(doc, 4, 2) && !model.isFolded(1));
        ensure("nothing to open", !model.unfold(doc, 4, 2));

        model.foldAll(doc, 4);
        ensure("every block", model.folded() == std::vector<S32>{ 0, 1 });
        ensure("opened for a line inside two", model.reveal(doc, 4, 2) && model.folded().empty());
        model.fold(doc, 4, 1);
        ensure("a line inside none opens nothing", !model.reveal(doc, 4, 4) && model.isFolded(1));
        model.unfoldAll();
        ensure("all open", model.folded().empty());
    }

    template<> template<>
    void alfoldmodel_object::test<3>()
    {
        set_test_name("folds slide with an edit, stay on the edit's first line, go inside it, and go with their blocks");
        ALTextDocument doc("a\nf()\n    x\n    y\ng()\n    z");
        model.fold(doc, 4, 1);
        model.fold(doc, 4, 4);
        ensure("two folded", model.folded() == std::vector<S32>{ 1, 4 });

        // Two lines put in above: both move down.
        ALTextDocument::Edit edit = doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)), "\nb\nc");
        model.edited(edit);
        ensure("slid", model.folded() == std::vector<S32>{ 3, 6 });
        ensure_equals("hiding what they did", lines(model.hidden(doc, 4)), std::string("4-5 7-7"));

        // Typing on a block's first line is not opening it.
        edit = doc.replace(ALTextRange(ALTextPos(3, 3), ALTextPos(3, 3)), " -- note");
        model.edited(edit);
        ensure("kept", model.isFolded(3));

        // Whole lines taken from above a folded block: it moves up with them.
        edit = doc.replace(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)), "");
        model.edited(edit);
        ensure("moved up with the lines", model.folded() == std::vector<S32>{ 1, 4 });

        // An edit through a folded block's first line takes the fold.
        edit = doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 3)), "");
        model.edited(edit);
        ensure("gone", model.folded() == std::vector<S32>{ 3 });

        // Its block gone -- the line under it outdented -- the fold goes.
        ensure_equals("the text now", doc.text(), std::string("a -- note\n    x\n    y\ng()\n    z"));
        edit = doc.replace(ALTextRange(ALTextPos(4, 0), ALTextPos(4, 4)), "");
        model.edited(edit);
        ensure("folded still", model.isFolded(3));
        ensure("nothing hidden once its block is gone", model.hidden(doc, 4).empty() && model.folded().empty());
    }

    template<> template<>
    void alfoldmodel_object::test<4>()
    {
        set_test_name("by syntax: what opens and closes, across lines; a brace alone its header's; a line that opens again after its close starts; a middle word; a comment's regions");
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
        ensure_equals("the state from its header, the if to before its else, the else to its close, the region", out, std::string("0-7 2-3 4-6 8-10"));
        ensure_equals("a blank line's indent is the next's", folds.indentOf(doc, 4, 3), 8);
        const std::vector<S32> open = folds.openAt(doc, 4, 5, 3);
        ensure("open at the else's line: the state, from its header, and the else", open.size() == 2 && open[0] == 0 && open[1] == 4);
        ensure("none at the region's line", folds.openAt(doc, 4, 9, 3).empty());

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
        ensure_equals("each arm a block of its own, the last through the end", out, std::string("0-1 2-3 4-6"));
    }

    template<> template<>
    void alfoldmodel_object::test<5>()
    {
        set_test_name("whole lines taken from a folded block's first line take its fold, and leave the block after them as it was; lines put in above it at its start take it down with them");
        const char* const text = "foo()\n{\n    x();\n}\nbar()\n{\n    y();\n}";
        ALTextDocument    doc(text);
        ensure_equals("two blocks", blocks(doc), std::string("0-3 4-7"));
        model.fold(doc, 4, 0);
        ALTextDocument::Edit edit = doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(4, 0)), "");
        model.edited(edit);
        ensure_equals("the block after them now first", doc.line(0), std::string("bar()"));
        ensure("the fold gone with its line, the block now there not folded for it", model.folded().empty());

        // Both folded: the one after moves up, and is one fold still.
        ALTextDocument both(text);
        ALFoldModel    two;
        two.fold(both, 4, 0);
        two.fold(both, 4, 4);
        edit = both.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(4, 0)), "");
        two.edited(edit);
        ensure("the one after moved up, once", two.folded() == std::vector<S32>{ 0 });
        ensure_equals("hiding its lines once", lines(two.hidden(both, 4)), std::string("1-3"));

        // Return at its first line's start: the line goes down, and the fold
        // with it.
        ALTextDocument above(text);
        ALFoldModel    down;
        down.fold(above, 4, 0);
        edit = above.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "\n");
        down.edited(edit);
        ensure("moved down with its line", down.folded() == std::vector<S32>{ 1 });
        ensure_equals("hiding what it did", lines(down.hidden(above, 4)), std::string("2-4"));
        edit = above.replace(ALTextRange(ALTextPos(1, 0), ALTextPos(1, 0)), "q");
        down.edited(edit);
        ensure("typing at its start keeps it", down.folded() == std::vector<S32>{ 1 });
    }

    template<> template<>
    void alfoldmodel_object::test<6>()
    {
        set_test_name("by syntax, open at a line: a bracket alone on its line under a header that opens a block of its own starts its own, once each, as the blocks have it");
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
        ensure_equals("the call's block from its line, the list's from its own", out, std::string("0-3 1-3"));
        ensure("both open inside them, each once", folds.openAt(doc, 4, 2, 8) == std::vector<S32>({ 0, 1 }));
        ensure("the innermost alone the list's", folds.openAt(doc, 4, 2, 1) == std::vector<S32>({ 1 }));
    }

    template<> template<>
    void alfoldmodel_object::test<7>()
    {
        set_test_name("by syntax, a line's blocks are read once and again only where it is said to have been lexed anew, the text lexed through first");
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
        ensure_equals("the blocks", found(), std::string("0-3 1-2 4-5"));
        ensure_equals("each line with anything on it asked once", asked, 6);
        ensure_equals("the text lexed through its last line first", lexed, doc.lineCount() - 1);
        folds.invalidate();
        quiet[1] = quiet[2] = true;
        ensure_equals("found again, none asked again", found(), std::string("0-3 1-2 4-5"));
        ensure_equals("asked no more", asked, 6);
        folds.relexed(1, 2);
        ensure_equals("those said lexed anew read again", found(), std::string("0-3 4-5"));
        ensure_equals("and they alone", asked, 8);
        ensure("the sticky headers lex through the line before theirs", folds.openAt(doc, 4, 5, 3) == std::vector<S32>({ 4 }) && lexed == 4);
    }
}
