/**
 * @file alxuifields.cpp
 * @brief A XUI attribute as the attribute grid shows it.
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

#include "alxuifields.h"

#include "alxuinotes.h"
#include "llfontgl.h"

#include <algorithm>
#include <initializer_list>

namespace
{
    bool oneOf(std::string_view name, std::initializer_list<std::string_view> names)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    bool builtFrom(std::string_view name, std::initializer_list<std::string_view> words)
    {
        return std::any_of(words.begin(), words.end(), [name](std::string_view word)
        {
            return name.find(word) != std::string_view::npos;
        });
    }

    // The section a name written in the notes belongs under, or -1 where
    // no one has written one.
    S32 sectionFromNotes(std::string_view tag, std::string_view name)
    {
        const ALXUINotes::Attribute* said = ALXUINotes::get().attribute(tag, name);
        if (!said || said->section.empty())
        {
            return -1;
        }
        if (said->section == "identity")   { return ALXUIFields::IDENTITY; }
        if (said->section == "geometry")   { return ALXUIFields::GEOMETRY; }
        if (said->section == "appearance") { return ALXUIFields::APPEARANCE; }
        if (said->section == "behaviour")  { return ALXUIFields::BEHAVIOUR; }
        if (said->section == "other")      { return ALXUIFields::OTHER; }
        return -1;
    }
}

namespace ALXUIFields
{
// XUI's vocabulary is wide but its shape is narrow: a fixed handful of
// names position a widget and a fixed handful name it, and what is left
// divides fairly well by the words the name is built from. A guess over a
// vocabulary is wrong somewhere, and where it is, the notes say so and are
// asked first: a short list of corrections beats a longer heuristic. A name
// none of the rules recognise is left in the last section rather than
// guessed at, which is what that section is for.
S32 sectionOf(std::string_view name, std::string_view tag)
{
    if (const S32 said = sectionFromNotes(tag, name); said >= 0)
    {
        return said;
    }
    const std::string_view head = name.substr(0, name.find('.'));

    if (oneOf(head, { "name", "label", "label_selected", "value", "initial_value", "title",
                      "short_title", "tool_tip", "help_topic", "filename", "menu_filename",
                      "class", "type" }))
    {
        return IDENTITY;
    }
    if (oneOf(head, { "left", "right", "top", "bottom", "width", "height", "rect",
                      "left_pad", "top_pad", "left_delta", "top_delta", "bottom_delta",
                      "follows", "layout", "orientation", "min_width", "max_width",
                      "min_height", "max_height", "min_dim", "max_dim", "expanded_min_dim",
                      "auto_resize", "user_resize", "border_size" }))
    {
        return GEOMETRY;
    }
    if (oneOf(head, { "enabled", "visible", "mouse_opaque", "tab_stop", "tab_group",
                      "default_tab_group", "read_only", "allow_text_entry", "chrome",
                      "single_instance", "reuse_instance", "can_close", "can_drag",
                      "can_minimize", "can_resize", "can_tear_off", "save_rect",
                      "save_visibility", "focus_root" }))
    {
        return BEHAVIOUR;
    }
    if (builtFrom(head, { "color", "image", "font", "texture", "bg_", "border", "highlight",
                          "shadow", "style", "halign", "valign" }))
    {
        return APPEARANCE;
    }
    if (builtFrom(head, { "width", "height", "_pad", "pad_", "margin", "spacing", "delta", "dim" }))
    {
        return GEOMETRY;
    }
    if (builtFrom(head, { "callback", "control", "enabled", "visible", "hover", "focus", "commit" }))
    {
        return BEHAVIOUR;
    }
    return OTHER;
}

// None of these is anything an author should have to remember, and none of
// them is known to the type system: all are strings as far as the block is
// concerned.
void vocabularyFor(ALPropertyGrid::Field& field)
{
    if (!field.values.empty())
    {
        return;
    }
    if (field.name == "font")
    {
        field.values = LLFontGL::getDeclaredFontNames();
    }
    else if (field.name == "font.size")
    {
        field.values = LLFontGL::getDeclaredSizeNames();
    }
    else if (field.name == "font.style")
    {
        field.values = { "BOLD", "ITALIC", "UNDERLINE" };
        field.flags = true;
        field.noneWord = "NORMAL";
    }
    else if (field.name == "layout")
    {
        // Which corner an element's numbers are measured from. Two
        // answers, and the file writes one of them as a word.
        field.values = { "topleft", "bottomleft" };
    }
    else if (field.name == "follows")
    {
        // Which edges of its parent the element is tied to: four answers,
        // written as one word, and drawn as what they do to it rather than
        // spelled. The order is the one the picture is drawn in and not the
        // one a file writes them in.
        field.values = { "left", "top", "right", "bottom" };
        field.edges = { "left", "bottom", "right", "top" };
        field.allWord = "all";
        field.noneWord = "none";
    }
}
}
