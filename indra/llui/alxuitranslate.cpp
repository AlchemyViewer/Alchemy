/**
 * @file alxuitranslate.cpp
 * @brief What a translator writes in a XUI file, whether it arrived, and how to write it.
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

#include "alxuitranslate.h"

#include "alxuicatalog.h"
#include "alxuiedit.h"

#include <algorithm>
#include <cctype>

namespace
{
    // The attributes the shipped overlays actually write.
    constexpr std::string_view TRANSLATABLE[] = {
        "title", "short_title", "value", "label", "label_selected", "tool_tip",
        "default_text", "ignoretext", "yestext", "notext", "canceltext",
        "description", "longdescription", "initial_value"
    };

    std::string trimmed(std::string_view text)
    {
        const size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
        {
            return std::string();
        }
        const size_t last = text.find_last_not_of(" \t\r\n");
        return std::string(text.substr(first, last - first + 1));
    }

    // The value with its [KEY] tokens taken out, which is what the rule
    // asks whether there is anything left of.
    std::string withoutPlaceholders(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '[')
            {
                const size_t close = text.find(']', i);
                if (close != std::string_view::npos)
                {
                    i = close;
                    continue;
                }
            }
            out += text[i];
        }
        return out;
    }

    bool hasLetterOrDigit(std::string_view text)
    {
        for (const char c : text)
        {
            if (std::isalnum((U8)c) || (U8)c >= 0x80)
            {
                return true;
            }
        }
        return false;
    }

    // "800 x 600" and its kin: a resolution, not a sentence.
    bool isResolution(std::string_view text)
    {
        bool seen_x = false;
        bool digits_before = false;
        bool digits_after = false;
        for (const char c : text)
        {
            if (std::isdigit((U8)c))
            {
                (seen_x ? digits_after : digits_before) = true;
            }
            else if ((c == 'x' || c == 'X') && !seen_x)
            {
                seen_x = true;
            }
            else if (c != ' ' && c != '\t')
            {
                return false;
            }
        }
        return seen_x && digits_before && digits_after;
    }

    bool allDigits(std::string_view text)
    {
        if (text.empty())
        {
            return false;
        }
        for (const char c : text)
        {
            if (!std::isdigit((U8)c))
            {
                return false;
            }
        }
        return true;
    }

    std::string textOf(pugi::xml_node element)
    {
        std::string text;
        for (pugi::xml_node child : element.children())
        {
            if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
            {
                text += child.value();
            }
        }
        return text;
    }

    // A path with an ordinal in it -- "name#2" -- names one of several
    // siblings the base calls the same thing. A translation cannot be
    // written to one of those from here: the ancestors this writes carry
    // a name and nothing else, so a second sibling of the same name is a
    // second ambiguity and not an address.
    bool repeatsAName(const std::vector<std::string>& path)
    {
        for (const std::string& step : path)
        {
            if (step.find('#') != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    // Whether one element is at or above another, which is the question
    // "would moving this into that put it inside itself".
    bool contains(pugi::xml_node outer, pugi::xml_node inner)
    {
        for (pugi::xml_node node = inner; node; node = node.parent())
        {
            if (node == outer)
            {
                return true;
            }
        }
        return false;
    }

    // The one element carrying a name below a root, and whether more than
    // one carries it.
    pugi::xml_node findByName(pugi::xml_node root, std::string_view name, bool& ambiguous)
    {
        pugi::xml_node found;
        ambiguous = false;
        std::vector<pugi::xml_node> stack{ root };
        while (!stack.empty())
        {
            const pugi::xml_node node = stack.back();
            stack.pop_back();
            for (pugi::xml_node child : node.children())
            {
                if (child.type() != pugi::node_element)
                {
                    continue;
                }
                stack.push_back(child);
                if (std::string_view(child.attribute("name").as_string()) == name)
                {
                    if (found)
                    {
                        ambiguous = true;
                    }
                    else
                    {
                        found = child;
                    }
                }
            }
        }
        return found;
    }
}

// static
bool ALXUITranslate::isTranslatableField(std::string_view name)
{
    return std::find(std::begin(TRANSLATABLE), std::end(TRANSLATABLE), name) != std::end(TRANSLATABLE);
}

// static
bool ALXUITranslate::forbidden(pugi::xml_node element)
{
    for (pugi::xml_node node = element; node && node.type() == pugi::node_element; node = node.parent())
    {
        if (std::string_view(node.attribute("translate").as_string()) == "false")
        {
            return true;
        }
    }
    return false;
}

// modified_strings.py's rule, which is the one the translation tables
// have been built from: a value with nothing but placeholders, digits, a
// resolution or no word in it at all is not a string anyone translates,
// and `value` is a hairball that means text only when the element has no
// text and no label of its own.
// static
bool ALXUITranslate::shouldTranslate(pugi::xml_node element, std::string_view field, std::string_view value)
{
    if (value.empty() || value.find("TestString PleaseIgnore") != std::string_view::npos)
    {
        return false;
    }
    const std::string bare = trimmed(withoutPlaceholders(value));
    if (bare.empty() || allDigits(bare) || !hasLetterOrDigit(bare) || isResolution(bare))
    {
        return false;
    }
    if (field == "value")
    {
        if (!textOf(element).empty() || element.attribute("label"))
        {
            return false;
        }
    }
    return true;
}

// static
std::vector<std::string> ALXUITranslate::placeholders(std::string_view text)
{
    std::vector<std::string> found;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '[')
        {
            continue;
        }
        const size_t close = text.find(']', i);
        if (close == std::string_view::npos)
        {
            break;
        }
        found.emplace_back(text.substr(i, close - i + 1));
        i = close;
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

void ALXUITranslate::clear()
{
    mUnits.clear();
}

S32 ALXUITranslate::count(State state) const
{
    S32 n = 0;
    for (const Unit& unit : mUnits)
    {
        n += unit.state == state;
    }
    return n;
}

void ALXUITranslate::scan(pugi::xml_node base, pugi::xml_node overlay)
{
    mUnits.clear();
    if (!base)
    {
        return;
    }
    scanBase(base, overlay);
    scanOverlay(base, overlay);
}

// Every field of the base a translator would write, and what the language
// has for it: the value at the same path, the value it put somewhere else,
// or nothing.
void ALXUITranslate::scanBase(pugi::xml_node base, pugi::xml_node overlay)
{
    std::vector<pugi::xml_node> stack{ base };
    while (!stack.empty())
    {
        const pugi::xml_node node = stack.back();
        stack.pop_back();
        for (pugi::xml_node child : node.children())
        {
            if (child.type() == pugi::node_element)
            {
                stack.push_back(child);
            }
        }

        const bool is_root = node == base;
        const path_t path = is_root ? path_t() : ALXUICatalog::namePath(node, /*any_tag=*/true);
        const std::string name = node.attribute("name").as_string();

        // Where the language put this element, if anywhere.
        pugi::xml_node mine;
        std::string elsewhere;
        Miss missed = Miss::None;
        if (overlay)
        {
            mine = is_root ? overlay : ALXUICatalog::resolve(overlay, path, /*any_tag=*/true);
            if (!mine && !name.empty())
            {
                bool ambiguous = false;
                if (pugi::xml_node moved = findByName(overlay, name, ambiguous))
                {
                    mine = moved;
                    missed = ambiguous ? Miss::Ambiguous : Miss::Moved;
                    const path_t at = ALXUICatalog::namePath(moved, /*any_tag=*/true);
                    for (const std::string& step : at)
                    {
                        elsewhere += elsewhere.empty() ? step : "/" + step;
                    }
                }
            }
        }

        auto add = [&](const std::string& field, const std::string& english, const std::string& translation,
                       bool have_translation)
        {
            Unit unit;
            unit.path = path;
            unit.tag = node.name();
            unit.field = field;
            unit.english = english;
            unit.translation = translation;
            unit.where = elsewhere;
            if (!have_translation)
            {
                unit.state = State::Missing;
            }
            else if (forbidden(node))
            {
                unit.state = State::Forbidden;
            }
            else if (missed != Miss::None)
            {
                unit.state = State::NotApplied;
                unit.miss = missed;
            }
            else if (placeholders(english) != placeholders(translation))
            {
                unit.state = State::Placeholders;
            }
            else
            {
                unit.state = State::Translated;
            }
            mUnits.push_back(std::move(unit));
        };

        for (pugi::xml_attribute attribute : node.attributes())
        {
            if (!isTranslatableField(attribute.name()))
            {
                continue;
            }
            const std::string english = attribute.value();
            const bool forbid = forbidden(node);
            if (!forbid && !shouldTranslate(node, attribute.name(), english))
            {
                continue;
            }
            pugi::xml_attribute theirs = mine ? mine.attribute(attribute.name()) : pugi::xml_attribute();
            if (forbid && !theirs)
            {
                // Nobody has broken the rule, so there is nothing to show.
                continue;
            }
            add(attribute.name(), english, theirs ? theirs.value() : std::string(), (bool)theirs);
        }

        const std::string text = textOf(node);
        const bool forbid = forbidden(node);
        if (!text.empty() && (forbid || shouldTranslate(node, std::string_view(), text)))
        {
            const std::string theirs = mine ? textOf(mine) : std::string();
            if (!forbid || !theirs.empty())
            {
                add(std::string(), text, theirs, mine && !theirs.empty());
            }
        }
    }
}

// What the language says that the base has no place for: an element it
// does not have, or an attribute that element does not carry. These are
// the values that vanish at build time, and the reason each one does.
void ALXUITranslate::scanOverlay(pugi::xml_node base, pugi::xml_node overlay)
{
    if (!overlay)
    {
        return;
    }

    std::vector<pugi::xml_node> stack{ overlay };
    while (!stack.empty())
    {
        const pugi::xml_node node = stack.back();
        stack.pop_back();
        for (pugi::xml_node child : node.children())
        {
            if (child.type() == pugi::node_element)
            {
                stack.push_back(child);
            }
        }
        if (node == overlay)
        {
            continue;
        }

        const std::string name = node.attribute("name").as_string();
        const path_t path = ALXUICatalog::namePath(node, /*any_tag=*/true);
        pugi::xml_node theirs = ALXUICatalog::resolve(base, path, /*any_tag=*/true);
        Miss missed = Miss::None;
        if (!theirs)
        {
            if (name.empty())
            {
                missed = Miss::Unnamed;
            }
            else
            {
                bool ambiguous = false;
                pugi::xml_node moved = findByName(base, name, ambiguous);
                theirs = moved;
                missed = !moved ? Miss::Absent : (ambiguous ? Miss::Ambiguous : Miss::Moved);
            }
        }

        auto add = [&](const std::string& field, const std::string& english, const std::string& translation, Miss why)
        {
            Unit unit;
            unit.path = theirs ? ALXUICatalog::namePath(theirs, /*any_tag=*/true) : path;
            unit.tag = node.name();
            unit.field = field;
            unit.english = english;
            unit.translation = translation;
            unit.state = State::NotApplied;
            unit.miss = why;
            for (const std::string& step : path)
            {
                unit.where += unit.where.empty() ? step : "/" + step;
            }
            mUnits.push_back(std::move(unit));
        };

        for (pugi::xml_attribute attribute : node.attributes())
        {
            if (!isTranslatableField(attribute.name()))
            {
                continue;
            }
            if (!theirs)
            {
                add(attribute.name(), std::string(), attribute.value(), missed);
            }
            else if (!theirs.attribute(attribute.name()))
            {
                // The element is there; this field of it is not.
                add(attribute.name(), std::string(), attribute.value(), Miss::AttributeAbsent);
            }
        }
    }
}

// The write. Three shapes, and the file decides which: the element is
// where the base says it should be, the language put it somewhere else,
// or it is not there at all.
// static
bool ALXUITranslate::write(ALXUIEdit& overlay, pugi::xml_node base, const Unit& unit,
                           const std::string& text, std::string& error)
{
    if (text.empty())
    {
        error = "an empty translation would blank what the base says";
        return false;
    }
    if (!unit.field.empty() && !isTranslatableField(unit.field))
    {
        error = unit.field + " is not a field a translation writes";
        return false;
    }
    if (repeatsAName(unit.path))
    {
        error = "the base calls more than one sibling by that name; write this one by hand";
        return false;
    }

    pugi::xml_node element = unit.path.empty() ? base : ALXUICatalog::resolve(base, unit.path, /*any_tag=*/true);
    if (!element)
    {
        error = "the base has no element at that path";
        return false;
    }
    if (forbidden(element))
    {
        error = "the base says this is not translated";
        return false;
    }
    if (!unit.field.empty() && !element.attribute(unit.field.c_str()))
    {
        error = "the base element does not carry " + unit.field;
        return false;
    }

    // The element in the overlay, wherever the language has put it, moved
    // under the ancestors the base gives it when that is not where it is.
    const std::string name = element.attribute("name").as_string();
    const path_t parent(unit.path.begin(), unit.path.end() - (unit.path.empty() ? 0 : 1));
    if (!unit.path.empty() && !overlay.resolve(unit.path))
    {
        bool ambiguous = false;
        pugi::xml_node moved = name.empty() ? pugi::xml_node() : findByName(overlay.root(), name, ambiguous);
        if (moved && ambiguous)
        {
            error = "the language writes " + name + " at more than one path; move it by hand";
            return false;
        }
        // The path is read before the chain is written: the insertion
        // reparses, and every node handle with it.
        const path_t from = moved ? ALXUICatalog::namePath(moved, /*any_tag=*/true) : path_t();
        if (!ensureChain(overlay, base, parent, error))
        {
            return false;
        }
        if (moved)
        {
            if (!overlay.moveElement(from, parent))
            {
                error = overlay.error();
                return false;
            }
        }
        else
        {
            // Nothing there: the element carrying nothing but its name,
            // under ancestors carrying nothing but theirs.
            const std::string xml = "<" + std::string(element.name()) + " name=\"" + name + "\"/>";
            if (!overlay.insertElement(parent, xml))
            {
                error = overlay.error();
                return false;
            }
        }
        if (!overlay.resolve(unit.path))
        {
            error = "the element could not be written where the base has it";
            return false;
        }
    }

    if (unit.field.empty())
    {
        if (!overlay.setText(unit.path, text))
        {
            error = overlay.error();
            return false;
        }
        return true;
    }
    if (!overlay.setAttribute(unit.path, unit.field, text))
    {
        error = overlay.error();
        return false;
    }
    return true;
}

// static
S32 ALXUITranslate::repair(ALXUIEdit& overlay, pugi::xml_node base, std::string& error)
{
    if (!base || !overlay.root())
    {
        return 0;
    }

    // A move can be blocked by an ancestor another move has not put in
    // place yet, so the pass runs again until it stops changing the file.
    // What the last pass could not do is what is left for a person.
    S32 total = 0;
    for (S32 pass = 0; pass < 8; ++pass)
    {
        error.clear();
        const S32 done = movePass(overlay, base, error);
        total += done;
        if (!done)
        {
            break;
        }
    }
    if (total)
    {
        prune(overlay);
    }
    return total;
}

// static
S32 ALXUITranslate::movePass(ALXUIEdit& overlay, pugi::xml_node base, std::string& error)
{
    ALXUITranslate units;
    units.scan(base, overlay.root());

    std::vector<Unit> moved;
    for (const Unit& unit : units.units())
    {
        if (unit.state == State::NotApplied && unit.miss == Miss::Moved
            && !unit.translation.empty() && !unit.path.empty()
            && !repeatsAName(unit.path))
        {
            moved.push_back(unit);
        }
    }

    S32 done = 0;
    for (const Unit& unit : moved)
    {
        std::string why;
        if (write(overlay, base, unit, unit.translation, why))
        {
            ++done;
        }
        else if (error.empty())
        {
            error = why;
        }
    }
    return done;
}

// What a move leaves behind: an element with nothing in it and nothing on
// it but a name. It says nothing to the merge and it was only ever the
// shell around what has gone, so it goes too. An element carrying
// anything else -- a value, which the merge matches unnamed children by,
// or a layout attribute someone meant -- stays.
// static
void ALXUITranslate::prune(ALXUIEdit& overlay)
{
    for (bool again = true; again;)
    {
        again = false;
        std::vector<pugi::xml_node> stack{ overlay.root() };
        while (!stack.empty() && !again)
        {
            const pugi::xml_node node = stack.back();
            stack.pop_back();
            for (pugi::xml_node child : node.children())
            {
                if (child.type() != pugi::node_element)
                {
                    continue;
                }
                stack.push_back(child);

                bool empty = true;
                for (pugi::xml_node grandchild : child.children())
                {
                    if (grandchild.type() == pugi::node_element
                        || (grandchild.type() == pugi::node_pcdata
                            && !trimmed(grandchild.value()).empty()))
                    {
                        empty = false;
                        break;
                    }
                }
                for (pugi::xml_attribute attribute : child.attributes())
                {
                    if (std::string_view(attribute.name()) != "name")
                    {
                        empty = false;
                        break;
                    }
                }
                if (!empty)
                {
                    continue;
                }
                const path_t path = ALXUICatalog::namePath(child, /*any_tag=*/true);
                if (path.empty() || repeatsAName(path))
                {
                    continue;
                }
                if (overlay.removeElement(path))
                {
                    // Every node of that parse is gone with it.
                    again = true;
                }
            }
        }
    }
}

// The ancestors a unit needs, in base order, each carrying nothing but
// its name: the merge matches on names, so a name is all a translation
// has to say about the way down to what it translates.
// static
bool ALXUITranslate::ensureChain(ALXUIEdit& overlay, pugi::xml_node base, const path_t& path, std::string& error)
{
    path_t so_far;
    for (const std::string& step : path)
    {
        so_far.push_back(step);
        if (overlay.resolve(so_far))
        {
            continue;
        }
        pugi::xml_node node = ALXUICatalog::resolve(base, so_far, /*any_tag=*/true);
        if (!node)
        {
            error = "the base has no element at " + step;
            return false;
        }
        const path_t parent(so_far.begin(), so_far.end() - 1);
        const std::string name = node.attribute("name").as_string();

        // The language may already have this ancestor, one level up or
        // three: it is moved, with everything under it, rather than made
        // a second time. A file that names one element twice is a file
        // the merge has to guess about, which is the thing this repair
        // exists to stop.
        bool ambiguous = false;
        pugi::xml_node elsewhere = name.empty() ? pugi::xml_node()
                                                : findByName(overlay.root(), name, ambiguous);
        if (elsewhere && ambiguous)
        {
            error = "the language names " + name + " more than once; move it by hand";
            return false;
        }
        if (elsewhere && contains(elsewhere, overlay.resolve(parent)))
        {
            // It is above where it would be going, so moving it would
            // put it inside itself.
            elsewhere = pugi::xml_node();
        }

        if (elsewhere)
        {
            const path_t from = ALXUICatalog::namePath(elsewhere, /*any_tag=*/true);
            if (!overlay.moveElement(from, parent))
            {
                error = overlay.error();
                return false;
            }
        }
        else
        {
            const std::string xml = "<" + std::string(node.name()) + " name=\"" + name + "\"/>";
            if (!overlay.insertElement(parent, xml))
            {
                error = overlay.error();
                return false;
            }
        }
        if (!overlay.resolve(so_far))
        {
            error = "the ancestor " + step + " could not be written where the base has it";
            return false;
        }
    }
    return true;
}
