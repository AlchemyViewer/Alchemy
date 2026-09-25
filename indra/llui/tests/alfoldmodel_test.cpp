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

#include "../alfoldmodel.h"

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
        model.edited(edit, 0, 0, 3);
        ensure("slid", model.folded() == std::vector<S32>{ 3, 6 });
        ensure_equals("hiding what they did", lines(model.hidden(doc, 4)), std::string("4-5 7-7"));

        // Typing on a block's first line is not opening it.
        edit = doc.replace(ALTextRange(ALTextPos(3, 3), ALTextPos(3, 3)), " -- note");
        model.edited(edit, 3, 3, 1);
        ensure("kept", model.isFolded(3));

        // Whole lines taken from above a folded block: it moves up with them.
        edit = doc.replace(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 0)), "");
        model.edited(edit, 1, 3, 1);
        ensure("moved up with the lines", model.folded() == std::vector<S32>{ 1, 4 });

        // An edit through a folded block's first line takes the fold.
        edit = doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 3)), "");
        model.edited(edit, 0, 1, 1);
        ensure("gone", model.folded() == std::vector<S32>{ 3 });

        // Its block gone -- the line under it outdented -- the fold goes.
        ensure_equals("the text now", doc.text(), std::string("a -- note\n    x\n    y\ng()\n    z"));
        edit = doc.replace(ALTextRange(ALTextPos(4, 0), ALTextPos(4, 4)), "");
        model.edited(edit, 4, 4, 1);
        ensure("folded still", model.isFolded(3));
        ensure("nothing hidden once its block is gone", model.hidden(doc, 4).empty() && model.folded().empty());
    }
}
