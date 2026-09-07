/**
 * @file alxuilint.cpp
 * @brief The checks over a built XUI tree and the document it was built from.
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

#include "alxuilint.h"

#include "alxuicatalog.h"
#include "alxuidiagnostics.h"
#include "alxuioverlay.h"
#include "alxuisourcemap.h"
#include "llbutton.h"
#include "lldraghandle.h"
#include "llfontgl.h"
#include "llresizebar.h"
#include "llresizehandle.h"
#include "lltextbox.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrl.h"
#include "llview.h"
#include "llviewborder.h"

#include <algorithm>
#include <cstdlib>

namespace
{
    // Two rects touching by a pixel or two are the usual borders and
    // dividers; an overlap worth reading about is larger than that.
    constexpr S32 OVERLAP_TOLERANCE = 2;

    bool rectsOverlap(const LLRect& a, const LLRect& b)
    {
        return a.mLeft <= b.mRight - OVERLAP_TOLERANCE
            && b.mLeft <= a.mRight - OVERLAP_TOLERANCE
            && a.mBottom <= b.mTop - OVERLAP_TOLERANCE
            && b.mBottom <= a.mTop - OVERLAP_TOLERANCE;
    }

    // A widget's own furniture, which sits over its box by design.
    bool isFurniture(const LLView* view)
    {
        return view->as<LLDragHandle>() || view->as<LLViewBorder>()
            || view->as<LLResizeBar>() || view->as<LLResizeHandle>();
    }

    S32 widestLine(const LLFontGL* font, const std::string& text)
    {
        S32 widest = 0;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos)
            {
                end = text.size();
            }
            widest = llmax(widest, font->getWidth(std::string_view(text).substr(start, end - start)));
            start = end + 1;
        }
        return widest;
    }

    bool endsWith(const std::string& text, const char* suffix)
    {
        const size_t n = std::strlen(suffix);
        return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
    }

    // A value that is a number, a list of numbers or a boolean says
    // nothing about a name, so a rule that looks up names passes over it.
    bool looksLikeName(const std::string& value)
    {
        if (value.empty() || value == "true" || value == "false")
        {
            return false;
        }
        bool any_letter = false;
        for (char c : value)
        {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            {
                any_letter = true;
            }
            else if (c == '/' || c == '\\' || c == '.')
            {
                // a path or a file name, which these rules do not resolve
                return false;
            }
        }
        return any_letter;
    }

    // A colour is a name or a literal of three or four numbers.
    bool isColorLiteral(const std::string& value)
    {
        S32 numbers = 0;
        const char* p = value.c_str();
        while (*p)
        {
            while (*p == ' ' || *p == ',' || *p == '\t')
            {
                ++p;
            }
            if (!*p)
            {
                break;
            }
            char* end = nullptr;
            std::strtod(p, &end);
            if (end == p)
            {
                return false;
            }
            ++numbers;
            p = end;
        }
        return numbers >= 3;
    }
}

// static
const char* ALXUILint::ruleName(Rule rule)
{
    switch (rule)
    {
    case Rule::ParseError:              return "parse";
    case Rule::BuildFailed:             return "build failed";
    case Rule::OverlayDrop:             return "overlay drop";
    case Rule::Overlap:                 return "overlap";
    case Rule::Alternatives:            return "alternatives";
    case Rule::OutOfBounds:             return "out of bounds";
    case Rule::NameCollision:           return "name collision";
    case Rule::EmptyRect:               return "empty rect";
    case Rule::Truncation:              return "truncation";
    case Rule::TemplateRootMismatch:    return "template root";
    case Rule::DanglingImage:           return "dangling image";
    case Rule::DanglingColor:           return "dangling colour";
    case Rule::DanglingFont:            return "dangling font";
    case Rule::CallbackNotRegistered:   return "callback";
    case Rule::ControlMissing:          return "control";
    case Rule::FileMissing:             return "file";
    }
    return "?";
}

// static
const char* ALXUILint::severityName(Severity severity)
{
    switch (severity)
    {
    case Severity::Error:   return "error";
    case Severity::Warning: return "warning";
    case Severity::Note:    return "note";
    }
    return "?";
}

// static
bool ALXUILint::callbackRegistered(const std::string& name)
{
    return LLUICtrl::CommitCallbackRegistry::instance().getValue(name) != nullptr
        || LLUICtrl::EnableCallbackRegistry::instance().getValue(name) != nullptr;
}

// static
bool ALXUILint::controlExists(const std::string& name, std::string* group)
{
    for (const auto& [group_name, controls] : LLUI::getInstance()->mSettingGroups)
    {
        if (controls && controls->controlExists(name))
        {
            if (group)
            {
                *group = group_name;
            }
            return true;
        }
    }
    return false;
}

void ALXUILint::clear()
{
    mFindings.clear();
    mCountByPath.clear();
}

void ALXUILint::add(Rule rule, Severity severity, const ALXUISelection::path_t& path,
                    std::string file, S32 line, std::string what, std::string message)
{
    Finding& f = mFindings.emplace_back();
    f.rule = rule;
    f.severity = severity;
    f.path = path;
    f.file = std::move(file);
    f.line = line;
    f.what = std::move(what);
    f.message = std::move(message);

    // The element and every ancestor, so a collapsed row carries what is
    // under it.
    ALXUISelection::path_t prefix(path);
    while (true)
    {
        ++mCountByPath[ALXUISelection::toString(prefix)];
        if (prefix.empty())
        {
            break;
        }
        prefix.pop_back();
    }
}

S32 ALXUILint::countUnder(const ALXUISelection::path_t& path) const
{
    auto it = mCountByPath.find(ALXUISelection::toString(path));
    return it == mCountByPath.end() ? 0 : it->second;
}

S32 ALXUILint::count(Severity severity) const
{
    S32 n = 0;
    for (const Finding& f : mFindings)
    {
        n += f.severity == severity;
    }
    return n;
}

void ALXUILint::run(const Input& input)
{
    clear();
    fromDiagnostics(input);
    fromOverlay(input);
    if (input.root)
    {
        walkViews(input, input.root, ALXUISelection::path_t());
    }
}

void ALXUILint::fromDiagnostics(const Input& input)
{
    if (!input.diagnostics)
    {
        return;
    }
    for (const ALXUIDiagnostics::Entry& e : input.diagnostics->entries())
    {
        // A child widget's attributes fail against its parent's block by
        // design and are parsed again by the child; that is the parser
        // working, not a defect.
        if (e.kind == ALXUIDiagnostics::Kind::UnknownAttribute && e.depth > 0)
        {
            const size_t dot = e.path.find('.');
            if (dot != std::string::npos && ALXUICatalog::isWidgetTag(e.path.substr(0, dot)))
            {
                continue;
            }
        }
        const bool fatal = e.kind == ALXUIDiagnostics::Kind::ParseError
                        || e.kind == ALXUIDiagnostics::Kind::CreateFailed
                        || e.kind == ALXUIDiagnostics::Kind::InvalidChild;
        add(fatal ? Rule::BuildFailed : Rule::ParseError,
            fatal ? Severity::Error : Severity::Warning,
            ALXUISelection::path_t(), e.file.empty() ? input.file : e.file, e.line, e.path,
            std::string(ALXUIDiagnostics::kindName(e.kind)) + (e.message.empty() ? "" : ": " + e.message));
    }
}

void ALXUILint::fromOverlay(const Input& input)
{
    if (!input.overlay)
    {
        return;
    }
    for (const ALXUIOverlay::Drop& d : input.overlay->drops())
    {
        add(Rule::OverlayDrop, Severity::Warning, ALXUISelection::fromString(d.path),
            input.overlay->layerPath(d.layer), d.line, d.what, d.why);
    }
}

void ALXUILint::walkViews(const Input& input, LLView* view, const ALXUISelection::path_t& path)
{
    checkGeometry(input, view, path);
    checkChildren(input, view, path);

    const ALXUISourceMap::Origin* origin = input.sourceMap ? input.sourceMap->find(view) : nullptr;
    if (origin && origin->node.notNull())
    {
        checkAttributes(input, view, path, origin->node.get());
        checkCallbacks(input, path, origin->line);
    }

    // Children in creation order, which is the order the file lists them.
    const LLView::child_list_t& children = *view->getChildList();
    boost::unordered_map<std::string, S32> seen;
    for (auto it = children.rbegin(); it != children.rend(); ++it)
    {
        LLView* child = *it;
        ALXUISelection::path_t child_path(path);
        child_path.push_back(ALXUISelection::step(child->getName(), seen[child->getName()]++));
        walkViews(input, child, child_path);
    }
}

void ALXUILint::checkGeometry(const Input& input, LLView* view, const ALXUISelection::path_t& path)
{
    // Only what the file describes: a widget's own children are not the
    // file's to fix.
    if (!input.sourceMap || !input.sourceMap->isFromXML(view))
    {
        return;
    }
    const ALXUISourceMap::Origin* origin = input.sourceMap->find(view);
    const S32 line = origin ? origin->line : 0;
    const LLRect& rect = view->getRect();

    if (view->getVisible() && (rect.getWidth() <= 0 || rect.getHeight() <= 0))
    {
        add(Rule::EmptyRect, Severity::Warning, path, input.file, line, view->getName(),
            "shown with a rect of " + std::to_string(rect.getWidth()) + " by " + std::to_string(rect.getHeight()));
    }

    if (const LLView* parent = view->getParent())
    {
        const LLRect bounds = parent->getLocalRect();
        if (view->getVisible() && parent->getVisible() && !isFurniture(view)
            && (rect.mLeft < bounds.mLeft || rect.mBottom < bounds.mBottom
                || rect.mRight > bounds.mRight || rect.mTop > bounds.mTop))
        {
            add(Rule::OutOfBounds, Severity::Warning, path, input.file, line, view->getName(),
                "sits outside " + parent->getName() + ", which is "
                + std::to_string(bounds.getWidth()) + " by " + std::to_string(bounds.getHeight()));
        }
    }

    // A label wider than the box it is in, which is the check "Show
    // Rectangles" asked the eye to make.
    const LLFontGL* font = nullptr;
    std::string text;
    S32 room = 0;
    if (const LLTextBox* box = view->as<LLTextBox>())
    {
        if (!box->getWordWrap())
        {
            font = box->getFont();
            text = box->getText();
            room = box->getRect().getWidth() - 2 * box->getHPad();
        }
    }
    else if (const LLButton* button = view->as<LLButton>())
    {
        font = button->getFont();
        text = button->getLabelUnselected();
        room = button->getRect().getWidth() - 8;
    }
    if (font && !text.empty() && view->getVisible())
    {
        const S32 width = widestLine(font, text);
        if (width > room)
        {
            add(Rule::Truncation, Severity::Warning, path, input.file, line, view->getName(),
                "the text needs " + std::to_string(width) + " pixels and has " + std::to_string(room));
        }
    }
}

void ALXUILint::checkChildren(const Input& input, LLView* view, const ALXUISelection::path_t& path)
{
    const LLView::child_list_t& children = *view->getChildList();
    if (children.size() < 2)
    {
        return;
    }

    std::vector<LLView*> named;
    for (auto it = children.rbegin(); it != children.rend(); ++it)
    {
        if (input.sourceMap && input.sourceMap->isFromXML(*it))
        {
            named.push_back(*it);
        }
    }

    boost::unordered_map<std::string, S32> counted;
    for (LLView* child : named)
    {
        if (++counted[child->getName()] == 2)
        {
            const ALXUISourceMap::Origin* origin = input.sourceMap->find(child);
            ALXUISelection::path_t child_path(path);
            child_path.push_back(ALXUISelection::step(child->getName(), 1));
            add(Rule::NameCollision, Severity::Warning, child_path, input.file,
                origin ? origin->line : 0, child->getName(),
                "a second child of " + view->getName() + " named this; getChild answers the first");
        }
    }

    for (size_t i = 0; i < named.size(); ++i)
    {
        if (isFurniture(named[i]))
        {
            continue;
        }
        for (size_t j = i + 1; j < named.size(); ++j)
        {
            if (isFurniture(named[j]) || !rectsOverlap(named[i]->getRect(), named[j]->getRect()))
            {
                continue;
            }
            // Files stack panels the code shows one at a time; those are
            // worth listing and are not defects.
            const bool both_shown = named[i]->getVisible() && named[j]->getVisible();
            const ALXUISourceMap::Origin* origin = input.sourceMap->find(named[i]);
            ALXUISelection::path_t child_path(path);
            child_path.push_back(ALXUISelection::step(named[i]->getName(), 0));
            add(both_shown ? Rule::Overlap : Rule::Alternatives,
                both_shown ? Severity::Warning : Severity::Note,
                child_path, input.file, origin ? origin->line : 0, named[i]->getName(),
                (both_shown ? "overlaps " : "overlaps the hidden ") + named[j]->getName());
        }
    }
}

void ALXUILint::checkAttributes(const Input& input, LLView* view, const ALXUISelection::path_t& path,
                                const LLXMLNode* node)
{
    for (const auto& [name_entry, attribute] : node->mAttributes)
    {
        const std::string name = name_entry->mString;
        const std::string value = attribute->getValue();
        const S32 line = attribute->getLineNumber() > 0 ? attribute->getLineNumber() : node->getLineNumber();
        if (!looksLikeName(value))
        {
            continue;
        }

        if (name == "font")
        {
            if (!LLFontGL::getFontByName(value))
            {
                add(Rule::DanglingFont, Severity::Warning, path, input.file, line, name,
                    "no font is named \"" + value + "\"");
            }
        }
        else if (name == "color" || endsWith(name, "_color"))
        {
            if (!isColorLiteral(value) && !LLUIColorTable::instance().colorExists(value))
            {
                add(Rule::DanglingColor, Severity::Warning, path, input.file, line, name,
                    "no colour is named \"" + value + "\"");
            }
        }
        else if (name.find("image") != std::string::npos && name != "image_overlay_alignment")
        {
            if (!LLUI::hasUIImage(value))
            {
                add(Rule::DanglingImage, Severity::Warning, path, input.file, line, name,
                    "textures.xml declares no \"" + value + "\"");
            }
        }
        else if (name == "control_name" || name == "control" || endsWith(name, "_control"))
        {
            if (!controlExists(value))
            {
                add(Rule::ControlMissing, Severity::Warning, path, input.file, line, name,
                    "no control group has a setting named \"" + value + "\"");
            }
        }
        else if (input.catalog && (name == "menu_filename" || name == "filename"))
        {
            if (!input.catalog->find(value))
            {
                add(Rule::FileMissing, Severity::Error, path, input.file, line, name,
                    "no XUI file is named \"" + value + "\"");
            }
        }
    }
}

void ALXUILint::checkCallbacks(const Input& input, const ALXUISelection::path_t& path, S32 line)
{
    if (!input.authored)
    {
        return;
    }
    const pugi::xml_node element = path.empty() ? input.authored
                                                : ALXUICatalog::resolve(input.authored, path);
    if (!element)
    {
        return;
    }

    // A callback is a child element carrying a function name: on_click,
    // commit_callback, menu_item_call.on_enable and the rest.
    for (pugi::xml_node child = element.first_child(); child; child = child.next_sibling())
    {
        if (child.type() != pugi::node_element)
        {
            continue;
        }
        const std::string function = child.attribute("function").as_string();
        if (function.empty() || callbackRegistered(function))
        {
            continue;
        }
        std::string tag = child.name();
        const size_t dot = tag.rfind('.');
        if (dot != std::string::npos)
        {
            tag = tag.substr(dot + 1);
        }
        // A floater registers most of its callbacks in its constructor,
        // which a shell build never runs, so there this is advisory.
        add(Rule::CallbackNotRegistered,
            input.callbacksAreDecisive ? Severity::Error : Severity::Note,
            path, input.file, line, tag,
            input.callbacksAreDecisive
                ? "no registry knows \"" + function + "\", so it does nothing"
                : "no global registry knows \"" + function + "\"; a floater's own are not registered here");
    }
}

// static
std::vector<ALXUILint::Finding> ALXUILint::checkCatalog(const ALXUICatalog& catalog)
{
    std::vector<Finding> findings;
    for (const ALXUICatalog::Entry& entry : catalog.entries())
    {
        for (const ALXUICatalog::Layer& layer : entry.layers)
        {
            if (!layer.doc)
            {
                Finding& f = findings.emplace_back();
                f.rule = Rule::ParseError;
                f.severity = Severity::Error;
                f.file = layer.path;
                f.line = layer.errorLine;
                f.what = entry.name;
                f.message = layer.error;
            }
        }

        if (entry.kind != ALXUICatalog::Kind::Template || entry.rootTag.empty())
        {
            continue;
        }
        // widgets/button.xml is read as the defaults for <button>, by
        // that name; a root of any other tag is read by nothing.
        std::string tag = entry.name.substr(entry.name.rfind('/') + 1);
        tag = tag.substr(0, tag.size() - 4);
        if (tag != entry.rootTag)
        {
            Finding& f = findings.emplace_back();
            f.rule = Rule::TemplateRootMismatch;
            f.severity = Severity::Error;
            f.file = entry.layers.empty() ? entry.name : entry.layers.front().path;
            f.line = 1;
            f.what = entry.rootTag;
            f.message = "a template for <" + tag + "> has a root of <" + entry.rootTag + ">, so nothing reads it";
        }
    }
    return findings;
}
