/**
 * @file alxuioverlay_test.cpp
 * @brief Which layers wrote a value, and which of them is the one in force.
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

#include "../alxuioverlay.h"

#include "llxmlnode.h"

#include "../test/lltut.h"

#include <string>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gOverlayTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gOverlayTestAnonName;
}

namespace tut
{
    struct alxuioverlay_data
    {
        // An attribute node, the way the merge holds one.
        static LLXMLNodePtr attribute(const std::string& name, const std::string& value, S32 line)
        {
            LLXMLNodePtr node = new LLXMLNode(name.c_str(), true);
            node->setValue(value);
            node->setLineNumber(line);
            return node;
        }
    };

    typedef test_group<alxuioverlay_data> alxuioverlay_test;
    typedef alxuioverlay_test::object     alxuioverlay_object;
    tut::alxuioverlay_test alxuioverlay_testgroup("alxuioverlay");

    // A value nobody wrote over has no writers: the base wrote it, and the
    // base is not a layer that overrode anything.
    template<> template<>
    void alxuioverlay_object::test<1>()
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        ensure("nothing wrote over it", overlay.writersOf(base.get()).empty());
        ensure("so nothing is its origin", overlay.originOf(base.get()) == nullptr);
    }

    // Every layer that wrote it is kept, in the order they were applied,
    // with what each of them said.
    template<> template<>
    void alxuioverlay_object::test<2>()
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        LLXMLNodePtr skin = attribute("width", "120", 7);
        LLXMLNodePtr language = attribute("width", "160", 9);

        overlay.attributeApplied(1, base.get(), skin.get());
        overlay.attributeApplied(2, base.get(), language.get());

        const std::vector<ALXUIOverlay::Origin>& writers = overlay.writersOf(base.get());
        ensure_equals("both layers are kept", (S32)writers.size(), 2);
        ensure_equals("the first is the one applied first", writers[0].layer, 1);
        ensure_equals("with what it said", writers[0].value, std::string("120"));
        ensure_equals("and the line it said it on", writers[0].line, 7);
        ensure_equals("the second is the later one", writers[1].layer, 2);
        ensure_equals("with what it said", writers[1].value, std::string("160"));
    }

    // The one in force is the last to have written it, which is what the
    // rest of the tool has always asked this for.
    template<> template<>
    void alxuioverlay_object::test<3>()
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        LLXMLNodePtr skin = attribute("width", "120", 7);
        LLXMLNodePtr language = attribute("width", "160", 9);

        overlay.attributeApplied(1, base.get(), skin.get());
        overlay.attributeApplied(2, base.get(), language.get());

        const ALXUIOverlay::Origin* winner = overlay.originOf(base.get());
        ensure("something wrote it", winner != nullptr);
        ensure_equals("the last one did", winner->layer, 2);
        ensure_equals("and the line is that layer's", winner->line, 9);
    }

    // Two attributes of the same element are two questions.
    template<> template<>
    void alxuioverlay_object::test<4>()
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr width = attribute("width", "100", 4);
        LLXMLNodePtr label = attribute("label", "Go", 5);
        LLXMLNodePtr overlaid = attribute("label", "Los", 11);

        overlay.attributeApplied(1, label.get(), overlaid.get());

        ensure_equals("the one written over has a writer", (S32)overlay.writersOf(label.get()).size(), 1);
        ensure("the one beside it has none", overlay.writersOf(width.get()).empty());
    }

    // Clearing forgets all of it, which is what a fresh build of the file
    // wants.
    template<> template<>
    void alxuioverlay_object::test<5>()
    {
        ALXUIOverlay overlay;
        LLXMLNodePtr base = attribute("width", "100", 4);
        LLXMLNodePtr skin = attribute("width", "120", 7);
        overlay.attributeApplied(1, base.get(), skin.get());
        overlay.clear();
        ensure("nothing is remembered", overlay.writersOf(base.get()).empty());
    }
}
