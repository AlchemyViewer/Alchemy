/**
 * @file alxuilint.h
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

#pragma once

#include "alxuidiagnostics.h"
#include "alxuiselection.h"

#include <string>
#include <vector>

#include <boost/unordered_map.hpp>
#include <pugixml.hpp>

class ALXUICatalog;
class ALXUIOverlay;
class ALXUISourceMap;
class LLView;
class LLXMLNode;

// What is wrong with a file, gathered from the three things that know:
// what the parser and the factory reported while building, what the layer
// merge dropped, and what the built tree and its document say when asked.
// Every finding carries the element's name path, so the tree can badge it
// and a double-click can select it, and the file and line it is on.
class ALXUILint
{
public:
    enum class Rule : U8
    {
        ParseError,             // the parser refused something
        BuildFailed,            // the factory could not build an element
        OverlayDrop,            // a layer applied to nothing
        Overlap,                // two siblings shown together intersect
        Alternatives,           // two siblings intersect, not shown together
        OutOfBounds,            // a child sits outside its parent
        NameCollision,          // two siblings of one name; getChild answers the first
        EmptyRect,              // a shown widget with no width or no height
        Truncation,             // a label wider than the box it is in
        TemplateRootMismatch,   // a widgets/ file whose root is not the tag it configures
        UnknownAttribute,       // an attribute the widget's parameter block has no name for
        DanglingImage,          // an image name textures.xml does not declare
        DanglingColor,          // a colour name colors.xml does not declare
        DanglingFont,           // a font name the font registry does not know
        CallbackNotRegistered,  // a function name no registry knows
        ControlMissing,         // a setting name no control group has
        FileMissing             // a referenced file the catalog does not have
    };

    enum class Severity : U8
    {
        Error,      // this element does not work
        Warning,    // this element probably does not do what the file says
        Note        // worth a look, and often on purpose
    };

    struct Finding
    {
        Rule                    rule = Rule::ParseError;
        Severity                severity = Severity::Warning;
        ALXUISelection::path_t  path;       // the element, for selecting and badging
        std::string             file;
        S32                     line = 0;
        std::string             what;       // the attribute, tag or name at fault
        std::string             message;
    };

    // What a run has to look at. The tree and the source map are the file
    // as built; the diagnostics and the overlay are what the build said
    // while it happened; the catalog answers whether a referenced file
    // exists.
    struct Input
    {
        LLView*                 root = nullptr;
        const ALXUISourceMap*   sourceMap = nullptr;
        // What the sink collected while the build ran; the sink itself
        // lives only as long as the build.
        const std::vector<ALXUIDiagnostics::Entry>* diagnostics = nullptr;
        const ALXUIOverlay*     overlay = nullptr;
        const ALXUICatalog*     catalog = nullptr;
        // The file as written. The parser deletes each parameter element
        // it consumes from the document it built from, so a callback is
        // not there to be read afterwards; this is where it still is.
        pugi::xml_node          authored;
        std::string             file;
        // A menu file's functions are registered at startup, so a name no
        // registry knows is decisive; a floater registers most of its own
        // in its constructor, which a shell build never runs, so there the
        // same evidence is only advisory.
        bool                    callbacksAreDecisive = false;
    };

    void run(const Input& input);
    void clear();

    const std::vector<Finding>& findings() const { return mFindings; }
    bool empty() const { return mFindings.empty(); }

    // How many findings are on an element and everything below it, which
    // is what a collapsed row shows.
    S32 countUnder(const ALXUISelection::path_t& path) const;
    S32 count(Severity severity) const;

    // The rules that need no build: a layer that did not parse, and a
    // file under widgets/ whose root tag is not the tag its file name
    // says it configures.
    static std::vector<Finding> checkCatalog(const ALXUICatalog& catalog);

    static const char* ruleName(Rule rule);
    static const char* severityName(Severity severity);

    // The evidence the reference rules and the Bindings inspector share,
    // so both answer from the same place.
    static bool callbackRegistered(const std::string& name);
    static bool controlExists(const std::string& name, std::string* group = nullptr);

private:
    void add(Rule rule, Severity severity, const ALXUISelection::path_t& path,
             std::string file, S32 line, std::string what, std::string message);
    void fromDiagnostics(const Input& input);
    void fromOverlay(const Input& input);
    void walkViews(const Input& input, LLView* view, const ALXUISelection::path_t& path);
    void checkGeometry(const Input& input, LLView* view, const ALXUISelection::path_t& path);
    void checkChildren(const Input& input, LLView* view, const ALXUISelection::path_t& path);
    void checkAttributes(const Input& input, LLView* view, const ALXUISelection::path_t& path,
                         const LLXMLNode* node);
    void checkCallbacks(const Input& input, const ALXUISelection::path_t& path, S32 line);

    std::vector<Finding>                        mFindings;
    boost::unordered_map<std::string, S32>      mCountByPath;   // the element and everything below it
};
