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
#include "alxuischema.h"
#include "alxuisourcemap.h"
#include "llbutton.h"
#include "lldraghandle.h"
#include "llfontgl.h"
#include "llfontregistry.h"
#include "lllayoutstack.h"
#include "llresizebar.h"
#include "llresizehandle.h"
#include "llstl.h"
#include "lluictrlfactory.h"
#include "llxmlnode.h"

#include <boost/unordered/unordered_flat_map.hpp>
#include "lltextbox.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "lluictrl.h"
#include "llview.h"
#include "llviewborder.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

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

    // A parent that lays its children out itself reads no position from
    // them, so a fix that moved or sized one would write a number nothing
    // reads -- and the file's own root sits wherever the tool put it. Both
    // are still worth reporting; neither is worth offering to mend.
    bool placedBySomethingElse(const LLView* view, const LLView* root)
    {
        const LLView* parent = view->getParent();
        return view == root || !parent || parent->as<LLLayoutStack>() != nullptr;
    }

    // A widget's own furniture, which sits over its box by design.
    bool isFurniture(const LLView* view)
    {
        return view->as<LLDragHandle>() || view->as<LLViewBorder>()
            || view->as<LLResizeBar>() || view->as<LLResizeHandle>();
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

namespace
{
    // Each rule's name, which the findings store keys by, and the line in
    // xui_lint.xml that says it in the language in force. One row per rule,
    // in the order of the enumeration, so a rule added there is added here
    // and nowhere else.
    struct RuleWords
    {
        ALXUILint::Rule rule;
        const char*     name;
        const char*     key;
    };
    constexpr RuleWords RULES[] = {
        { ALXUILint::Rule::ParseError,            "parse",                "LintRuleParseError" },
        { ALXUILint::Rule::BuildFailed,           "build failed",         "LintRuleBuildFailed" },
        { ALXUILint::Rule::OverlayDrop,           "overlay drop",         "LintRuleOverlayDrop" },
        { ALXUILint::Rule::OverlayRescue,         "overlay rescue",       "LintRuleOverlayRescue" },
        { ALXUILint::Rule::Overlap,               "overlap",              "LintRuleOverlap" },
        { ALXUILint::Rule::Alternatives,          "alternatives",         "LintRuleAlternatives" },
        { ALXUILint::Rule::OutOfBounds,           "out of bounds",        "LintRuleOutOfBounds" },
        { ALXUILint::Rule::NameCollision,         "name collision",       "LintRuleNameCollision" },
        { ALXUILint::Rule::LayoutDimension,       "layout dimension",     "LintRuleLayoutDimension" },
        { ALXUILint::Rule::EmptyRect,             "empty rect",           "LintRuleEmptyRect" },
        { ALXUILint::Rule::Truncation,            "truncation",           "LintRuleTruncation" },
        { ALXUILint::Rule::TemplateRootMismatch,  "template root",        "LintRuleTemplateRootMismatch" },
        { ALXUILint::Rule::UnknownAttribute,      "unknown attribute",    "LintRuleUnknownAttribute" },
        { ALXUILint::Rule::WroteTheDefault,       "wrote the default",    "LintRuleWroteTheDefault" },
        { ALXUILint::Rule::DeprecatedAttribute,   "deprecated attribute", "LintRuleDeprecatedAttribute" },
        { ALXUILint::Rule::DanglingImage,         "dangling image",       "LintRuleDanglingImage" },
        { ALXUILint::Rule::DanglingColor,         "dangling colour",      "LintRuleDanglingColor" },
        { ALXUILint::Rule::DanglingFont,          "dangling font",        "LintRuleDanglingFont" },
        { ALXUILint::Rule::CallbackNotRegistered, "callback",             "LintRuleCallbackNotRegistered" },
        { ALXUILint::Rule::ControlMissing,        "control",              "LintRuleControlMissing" },
        { ALXUILint::Rule::FileMissing,           "file",                 "LintRuleFileMissing" },
    };
    static_assert(std::size(RULES) == (size_t)ALXUILint::Rule::FileMissing + 1, "a rule without its words");

    const RuleWords& wordsOf(ALXUILint::Rule rule)
    {
        const size_t at = (size_t)rule;
        return RULES[at < std::size(RULES) && RULES[at].rule == rule ? at : 0];
    }
}

// static
const char* ALXUILint::ruleName(Rule rule)
{
    return wordsOf(rule).name;
}

// static
bool ALXUILint::ruleNamed(std::string_view name, Rule& rule)
{
    for (const RuleWords& words : RULES)
    {
        if (name == words.name)
        {
            rule = words.rule;
            return true;
        }
    }
    return false;
}

// static
const char* ALXUILint::ruleKey(Rule rule)
{
    return wordsOf(rule).key;
}

// static
const char* ALXUILint::severityKey(Severity severity)
{
    switch (severity)
    {
    case Severity::Error:   return "LintSeverityError";
    case Severity::Warning: return "LintSeverityWarning";
    case Severity::Note:    return "LintSeverityNote";
    }
    return "LintSeverityNote";
}

// static
const std::vector<const char*>& ALXUILint::keys()
{
    static const std::vector<const char*> said = {
        "LintDiagParseError",
        "LintDiagParseWarning",
        "LintDiagUnknownAttribute",
        "LintDiagMisScopedElement",
        "LintDiagInvalidChild",
        "LintDiagCreateFailed",
        "LintDropFileUnparsed",
        "LintDropRootName",
        "LintDropRootTag",
        "LintDropUnnamed",
        "LintDropDuplicate",
        "LintDropAmbiguous",
        "LintDropNotBelow",
        "LintDropTextEmpty",
        "LintDropAttribute",
        "LintRescued",
        "LintEmptyRect",
        "LintOutOfBounds",
        "LintTruncation",
        "LintNameCollision",
        "LintNameCollisionParameter",
        "LintOverlap",
        "LintOverlapHidden",
        "LintUnknownAttribute",
        "LintUnknownAttributeSlip",
        "LintDeprecated",
        "LintDeprecatedFor",
        "LintWroteTheDefault",
        "LintFileMissing",
        "LintDanglingFont",
        "LintDanglingColor",
        "LintDanglingImage",
        "LintControlMissing",
        "LintLayoutTwoNames",
        "LintLayoutHorizontalMin",
        "LintLayoutHorizontalMax",
        "LintLayoutVerticalMin",
        "LintLayoutVerticalMax",
        "LintCallbackUnknown",
        "LintCallbackNotGlobal",
        "LintLayerUnparsed",
        "LintTemplateRoot",
    };
    return said;
}

namespace
{
    const char* const WORDS_FILE = "xui_lint.xml";

    boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<> >& wordsHeld()
    {
        static boost::unordered_flat_map<std::string, std::string, ll::string_hash, std::equal_to<> > held;
        return held;
    }
    bool sWordsRead = false;
}

// static
void ALXUILint::readWords()
{
    if (sWordsRead)
    {
        return;
    }
    sWordsRead = true;
    LLXMLNodePtr root;
    if (!LLUICtrlFactory::getLayeredXMLNode(WORDS_FILE, root) || root.isNull())
    {
        return;
    }
    for (LLXMLNodePtr child = root->getFirstChild(); child.notNull(); child = child->getNextSibling())
    {
        std::string name;
        if (child->hasName("string") && child->getAttributeString("name", name) && !name.empty())
        {
            wordsHeld()[name] = child->getTextContents();
        }
    }
}

// static
std::string ALXUILint::wording(std::string_view key, const LLStringUtil::format_map_t& args)
{
    readWords();
    const auto found = wordsHeld().find(key);
    if (found == wordsHeld().end())
    {
        std::string bare(key);
        for (const auto& [name, value] : args)
        {
            bare += " " + value();
        }
        return bare;
    }
    std::string text = found->second;
    LLStringUtil::format(text, args);
    return text;
}

// static
std::string ALXUILint::ruleLabel(Rule rule)
{
    return wording(ruleKey(rule), LLStringUtil::format_map_t());
}

// static
std::string ALXUILint::severityLabel(Severity severity)
{
    return wording(severityKey(severity), LLStringUtil::format_map_t());
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
S32 ALXUILint::widestLine(const LLFontGL* font, const std::string& text)
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

bool ALXUILint::Finding::applyFix(ALXUIEdit& document, const ALXUIEdit::Anchor& now) const
{
    switch (fix.did)
    {
    case Fix::Do::TakeAttributeOut: return document.removeAttribute(path, what);
    case Fix::Do::SpellAttribute:   return document.renameAttribute(path, what, fix.spelling);
    case Fix::Do::MoveInside:       return document.translate(path, fix.dx, fix.dy, now);
    case Fix::Do::WidenBy:          return document.resize(path, fix.dx, 0, now);
    case Fix::Do::Nothing:          break;
    }
    return false;
}

void ALXUILint::clear()
{
    mFindings.clear();
    mCountByPath.clear();
    mFontFamilies.clear();
}

ALXUILint::Finding& ALXUILint::add(Rule rule, Severity severity, const ALXUISelection::path_t& path,
                                   std::string file, S32 line, std::string what, const char* key,
                                   LLStringUtil::format_map_t args)
{
    Finding& f = mFindings.emplace_back();
    f.rule = rule;
    f.severity = severity;
    f.path = path;
    f.file = std::move(file);
    f.line = line;
    f.what = std::move(what);
    f.key = key;
    f.args = std::move(args);
    f.message = wording(f.key, f.args);

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
    return f;
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
    for (const ALXUIDiagnostics::Entry& e : *input.diagnostics)
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
        const char* key = "LintDiagParseError";
        switch (e.kind)
        {
        case ALXUIDiagnostics::Kind::ParseError:       break;
        case ALXUIDiagnostics::Kind::ParseWarning:     key = "LintDiagParseWarning"; break;
        case ALXUIDiagnostics::Kind::UnknownAttribute: key = "LintDiagUnknownAttribute"; break;
        case ALXUIDiagnostics::Kind::MisScopedElement: key = "LintDiagMisScopedElement"; break;
        case ALXUIDiagnostics::Kind::InvalidChild:     key = "LintDiagInvalidChild"; break;
        case ALXUIDiagnostics::Kind::CreateFailed:     key = "LintDiagCreateFailed"; break;
        }
        add(fatal ? Rule::BuildFailed : Rule::ParseError,
            fatal ? Severity::Error : Severity::Warning,
            ALXUISelection::path_t(), e.file.empty() ? input.file : e.file, e.line, e.path,
            key, { { "[MESSAGE]", e.message } });
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
            input.overlay->layerPath(d.layer), d.line, d.what, d.key.c_str(), d.args);
    }
    // Applied, and worth a look: a value found where the base moved the
    // element is found only until the base grows another of the name.
    for (const ALXUIOverlay::Rescue& r : input.overlay->rescues())
    {
        add(Rule::OverlayRescue, Severity::Note, ALXUISelection::fromString(r.to),
            input.overlay->layerPath(r.layer), r.line, r.from, "LintRescued",
            { { "[FROM]", r.from }, { "[TO]", r.to } });
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
        checkLayoutDimensions(input, view, path, origin->node.get());
        checkCallbacks(input, path, origin->line);
        checkParameterNames(input, path, origin->line);
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
        add(Rule::EmptyRect, Severity::Warning, path, input.file, line, view->getName(), "LintEmptyRect",
            { { "[WIDTH]", std::to_string(rect.getWidth()) }, { "[HEIGHT]", std::to_string(rect.getHeight()) } });
    }

    // The root's parent is whatever the tool built it into -- a canvas, a
    // window -- and how big that is says nothing about the file. Only what
    // the file puts inside something else the file describes is checked.
    if (const LLView* parent = view != input.root ? view->getParent() : nullptr)
    {
        const LLRect bounds = parent->getLocalRect();
        if (view->getVisible() && parent->getVisible() && !isFurniture(view)
            && (rect.mLeft < bounds.mLeft || rect.mBottom < bounds.mBottom
                || rect.mRight > bounds.mRight || rect.mTop > bounds.mTop))
        {
            Finding& f = add(Rule::OutOfBounds, Severity::Warning, path, input.file, line, view->getName(),
                "LintOutOfBounds", { { "[PARENT]", parent->getName() },
                                     { "[WIDTH]", std::to_string(bounds.getWidth()) },
                                     { "[HEIGHT]", std::to_string(bounds.getHeight()) } });

            // Moving it in only works where it fits: something wider than
            // what holds it leaves by the other edge whichever way it goes,
            // and offering to move that is offering to move it forever.
            if (rect.getWidth() <= bounds.getWidth() && rect.getHeight() <= bounds.getHeight()
                && !placedBySomethingElse(view, input.root))
            {
                f.fix.did = Fix::Do::MoveInside;
                f.fix.dx = rect.mLeft < bounds.mLeft ? bounds.mLeft - rect.mLeft
                         : rect.mRight > bounds.mRight ? bounds.mRight - rect.mRight : 0;
                f.fix.dy = rect.mBottom < bounds.mBottom ? bounds.mBottom - rect.mBottom
                         : rect.mTop > bounds.mTop ? bounds.mTop - rect.mTop : 0;
            }
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
            Finding& f = add(Rule::Truncation, Severity::Warning, path, input.file, line, view->getName(),
                "LintTruncation", { { "[NEEDED]", std::to_string(width) }, { "[ROOM]", std::to_string(room) } });
            if (!placedBySomethingElse(view, input.root))
            {
                f.fix.did = Fix::Do::WidenBy;
                f.fix.dx = width - room;
            }
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
            // Its path as every other path is counted, among all the
            // siblings and not only the ones the file wrote: a widget's
            // own child of the same name comes into the count.
            ALXUISelection::path_t child_path;
            if (!ALXUISelection::pathOf(child, input.root, child_path))
            {
                child_path = path;
                child_path.push_back(ALXUISelection::step(child->getName(), 1));
            }
            add(Rule::NameCollision, Severity::Warning, child_path, input.file,
                origin ? origin->line : 0, child->getName(),
                "LintNameCollision", { { "[PARENT]", view->getName() } });
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
            // Its own path, ordinal and all: the second of two siblings of
            // one name is not the first of them.
            ALXUISelection::path_t child_path;
            if (!ALXUISelection::pathOf(named[i], input.root, child_path))
            {
                child_path = path;
                child_path.push_back(ALXUISelection::step(named[i]->getName(), 0));
            }
            add(both_shown ? Rule::Overlap : Rule::Alternatives,
                both_shown ? Severity::Warning : Severity::Note,
                child_path, input.file, origin ? origin->line : 0, named[i]->getName(),
                both_shown ? "LintOverlap" : "LintOverlapHidden", { { "[OTHER]", named[j]->getName() } });
        }
    }
}

void ALXUILint::checkAttributes(const Input& input, LLView* view, const ALXUISelection::path_t& path,
                                const LLXMLNode* node)
{
    // What the widget answers to. The element's own tag is not the question:
    // <panel class="foo"> builds foo, and foo's parameters are what the
    // attributes here were parsed against. A class the schema does not know
    // is a class no registry names, and nothing here can be said about it.
    const std::string* tag = LLUICtrlFactory::widgetTag(view->viewType());
    const ALXUISchema::Tag* schema = tag ? ALXUISchema::get().tag(*tag) : nullptr;

    for (const auto& [name_entry, attribute] : node->mAttributes)
    {
        const std::string name = name_entry->mString;
        const std::string value = attribute->getValue();
        const S32 line = attribute->getLineNumber() > 0 ? attribute->getLineNumber() : node->getLineNumber();

        if (schema && !ALXUISchema::get().accepts(schema->name, name))
        {
            // A name one edit from a real one was meant to be that one, and
            // spelling it right keeps what the author wrote. Nothing near
            // enough, and all that is left is that the file says something
            // nothing reads.
            const std::string meant = ALXUISchema::get().nearestSpelling(schema->name, name);
            Finding& f = add(Rule::UnknownAttribute, Severity::Warning, path, input.file, line, name,
                meant.empty() ? "LintUnknownAttribute" : "LintUnknownAttributeSlip",
                { { "[TAG]", schema->name }, { "[NAME]", name }, { "[MEANT]", meant } });
            if (!meant.empty())
            {
                f.fix.did = Fix::Do::SpellAttribute;
                f.fix.spelling = meant;
            }
            else
            {
                f.fix.did = Fix::Do::TakeAttributeOut;
            }
        }

        const ALXUISchema::Attribute* said =
            schema ? ALXUISchema::get().attribute(schema->name, name) : nullptr;

        // A name that works and should not be used. The block registered two
        // names for one parameter, which said that both work and will keep
        // working; a person wrote down which of them to write. Nothing is
        // broken here, so it is worth a look rather than a fault -- and it is
        // one edit away from being right, which the fix offers to make.
        if (said && said->deprecated)
        {
            Finding& f = add(Rule::DeprecatedAttribute, Severity::Note, path, input.file, line, name,
                said->instead.empty() ? "LintDeprecated" : "LintDeprecatedFor",
                { { "[NAME]", name }, { "[INSTEAD]", said->instead } });
            if (!said->instead.empty() && !node->hasAttribute(said->instead.c_str()))
            {
                f.fix.did = Fix::Do::SpellAttribute;
                f.fix.spelling = said->instead;
            }
        }

        // Written as the value this widget already carries -- and carries
        // because somebody chose it for this widget, not because it is where
        // the type starts: `left="0"` says where a thing goes and repeats
        // nobody. Often on purpose even so, a number written where a reader
        // would otherwise have to know it, so this is worth a look rather
        // than a fault. Compared as the two files spell it, so a value spelt
        // another way than the template spells it is passed over rather than
        // guessed at.
        if (said && said->declared && said->held == value)
        {
            add(Rule::WroteTheDefault, Severity::Note, path, input.file, line, name,
                "LintWroteTheDefault", { { "[NAME]", name } })
                .fix.did = Fix::Do::TakeAttributeOut;
        }

        // A file named is a file the catalog has or has not: asked before
        // the rule below turns a file name away for being one.
        if (input.catalog && (name == "menu_filename" || name == "filename"))
        {
            if (!value.empty() && !input.catalog->find(value))
            {
                add(Rule::FileMissing, Severity::Error, path, input.file, line, name,
                    "LintFileMissing", { { "[VALUE]", value } });
            }
            continue;
        }

        if (!looksLikeName(value))
        {
            continue;
        }

        if (name == "font")
        {
            // The two steps the font parameter takes, in its order: the
            // four legacy names, then the registry by the family a name
            // normalizes to, which is what every name in fonts.xml answers
            // to. Asked of the list of families rather than for the font,
            // since asking for one makes one, and a name nobody declared
            // would be made and warned about on every pass.
            if (!LLFontGL::getFontByName(value) && !fontDeclared(value))
            {
                add(Rule::DanglingFont, Severity::Warning, path, input.file, line, name,
                    "LintDanglingFont", { { "[VALUE]", value } });
            }
        }
        else if (name == "color" || endsWith(name, "_color"))
        {
            if (!isColorLiteral(value) && !LLUIColorTable::instance().colorExists(value))
            {
                add(Rule::DanglingColor, Severity::Warning, path, input.file, line, name,
                    "LintDanglingColor", { { "[VALUE]", value } });
            }
        }
        else if (name.find("image") != std::string::npos && name != "image_overlay_alignment")
        {
            if (!LLUI::hasUIImage(value))
            {
                add(Rule::DanglingImage, Severity::Warning, path, input.file, line, name,
                    "LintDanglingImage", { { "[VALUE]", value } });
            }
        }
        else if (name == "control_name" || name == "control" || endsWith(name, "_control"))
        {
            if (!controlExists(value))
            {
                add(Rule::ControlMissing, Severity::Warning, path, input.file, line, name,
                    "LintControlMissing", { { "[VALUE]", value } });
            }
        }
    }
}

// Whether fonts.xml declares the family a name normalizes to. The list
// is asked for once per run: a run is one file and a file names a font
// on many of its elements.
bool ALXUILint::fontDeclared(const std::string& name)
{
    if (mFontFamilies.empty())
    {
        const std::vector<std::string> declared = LLFontGL::getDeclaredFontNames();
        mFontFamilies.insert(declared.begin(), declared.end());
    }
    const std::string family = LLFontDescriptor(name, LLStringUtil::null, 0).normalize().getName();
    return !family.empty() && mFontFamilies.count(family) > 0;
}

// A layout panel has one dimension the stack reads -- the one along the axis
// the stack runs -- and three names for it: min_dim and its two synonyms
// min_width and min_height, and the same for max. So a name is either the one
// for the axis the stack actually lays out along, or it is the same parameter
// under a name that says the other axis; and two of them on one panel is one
// value, chosen by which name sorts last.
void ALXUILint::checkLayoutDimensions(const Input& input, LLView* view, const ALXUISelection::path_t& path,
                                      const LLXMLNode* node)
{
    LLLayoutPanel* panelp = view->as<LLLayoutPanel>();
    LLLayoutStack* stackp = panelp ? panelp->getParentAs<LLLayoutStack>() : nullptr;
    if (!stackp)
    {
        return;
    }

    const bool horizontal = stackp->getOrientation() == LLView::HORIZONTAL;
    const char* const across = horizontal ? "height" : "width";

    // The node holds its attributes sorted by name, which is the order the
    // parser applies them in, so the last of a group is the one left standing.
    for (const char* bound : { "min", "max" })
    {
        std::vector<std::pair<std::string, LLXMLNodePtr>> named;
        for (const auto& [name_entry, attribute] : node->mAttributes)
        {
            const std::string name = name_entry->mString;
            if (name == std::string(bound) + "_dim"
                || name == std::string(bound) + "_width"
                || name == std::string(bound) + "_height")
            {
                named.emplace_back(name, attribute);
            }
        }
        if (named.empty())
        {
            continue;
        }
        const std::string& winner = named.back().first;

        for (const auto& [name, attribute] : named)
        {
            const S32 line = attribute->getLineNumber() > 0 ? attribute->getLineNumber() : node->getLineNumber();
            if (name != winner)
            {
                add(Rule::LayoutDimension, Severity::Warning, path, input.file, line, name,
                    "LintLayoutTwoNames", { { "[NAME]", name }, { "[WINNER]", winner } });
            }
            else if (name == std::string(bound) + "_" + across)
            {
                const bool least = std::string_view(bound) == "min";
                add(Rule::LayoutDimension, Severity::Warning, path, input.file, line, name,
                    horizontal ? (least ? "LintLayoutHorizontalMin" : "LintLayoutHorizontalMax")
                               : (least ? "LintLayoutVerticalMin" : "LintLayoutVerticalMax"),
                    { { "[NAME]", name } });
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
            input.callbacksAreDecisive ? "LintCallbackUnknown" : "LintCallbackNotGlobal",
            { { "[FUNCTION]", function } });
    }
}

// A parameter element and a widget under one parent sharing a name. The
// parser tells them apart by their tags; the layer merge matches children
// by name whatever the tag, so a translation of either lands on whichever
// comes first, and a path made of names cannot say which of the two it
// means. Read off the file as written, since the parser has consumed the
// parameter elements by the time the tree is walked.
void ALXUILint::checkParameterNames(const Input& input, const ALXUISelection::path_t& path, S32 line)
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
    // The first child of each key, and whether it was a widget.
    boost::unordered_map<std::string, std::pair<pugi::xml_node, bool> > first;
    for (pugi::xml_node child = element.first_child(); child; child = child.next_sibling())
    {
        if (child.type() != pugi::node_element)
        {
            continue;
        }
        const std::string key(ALXUICatalog::keyOf(child, /*any_tag=*/true));
        if (key == "unnamed")
        {
            continue;
        }
        const bool widget = ALXUICatalog::isWidgetTag(child.name());
        const auto held = first.find(key);
        if (held == first.end())
        {
            first.emplace(key, std::make_pair(child, widget));
            continue;
        }
        if (held->second.second == widget)
        {
            continue;   // two widgets is the collision rule's, two parameters the merge's
        }
        add(Rule::NameCollision, Severity::Warning, path, input.file, line, key,
            "LintNameCollisionParameter",
            { { "[NAME]", key },
              { "[TAG]", held->second.first.name() },
              { "[OTHER]", child.name() },
              { "[PARENT]", std::string(ALXUICatalog::keyOf(element, /*any_tag=*/true)) } });
        // Said once per name.
        held->second.second = widget;
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
                f.key = "LintLayerUnparsed";
                f.args["[MESSAGE]"] = layer.error;
                f.message = wording(f.key, f.args);
            }
        }

        if (entry.kind != ALXUICatalog::Kind::Template || entry.rootTag.empty())
        {
            continue;
        }
        // widgets/button.xml is read as the defaults for <button>, found
        // by that file name. The parser takes the root's attributes
        // whatever the root is called, so a root of another tag still
        // applies; what it does is describe a widget that is not the one
        // being configured, and its dotted children hang off that name
        // rather than the widget's.
        std::string tag = entry.name.substr(entry.name.rfind('/') + 1);
        tag = tag.substr(0, tag.size() - 4);
        if (tag != entry.rootTag)
        {
            Finding& f = findings.emplace_back();
            f.rule = Rule::TemplateRootMismatch;
            f.severity = Severity::Note;
            f.file = entry.layers.empty() ? entry.name : entry.layers.front().path;
            f.line = 1;
            f.what = entry.rootTag;
            f.key = "LintTemplateRoot";
            f.args["[TAG]"] = tag;
            f.args["[ROOT]"] = entry.rootTag;
            f.message = wording(f.key, f.args);
        }
    }
    return findings;
}
