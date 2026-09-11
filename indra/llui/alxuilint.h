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
#include "alxuiedit.h"
#include "alxuiselection.h"
#include "llstring.h"

#include <string>
#include <vector>

#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <pugixml.hpp>

class ALXUICatalog;
class ALXUIOverlay;
class ALXUISourceMap;
class LLFontGL;
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
        OverlayRescue,          // a layer applied where the base moved the element, not where it was written
        Overlap,                // two siblings shown together intersect
        Alternatives,           // two siblings intersect, not shown together
        OutOfBounds,            // a child sits outside its parent
        NameCollision,          // two siblings of one name; getChild answers the first
        LayoutDimension,        // a layout panel's dimension read along the other axis, or twice
        EmptyRect,              // a shown widget with no width or no height
        Truncation,             // a label wider than the box it is in
        TemplateRootMismatch,   // a widgets/ file whose root is not the tag it configures
        UnknownAttribute,       // an attribute the widget's parameter block has no name for
        WroteTheDefault,        // an attribute written as the value it already carries
        DeprecatedAttribute,    // a name that works, and that nothing new should use
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

    // What would put a finding right: one operation of the kinds ALXUIEdit
    // already has, on the element the finding names.
    //
    // Said in the terms a document works in and not in words, because the
    // words a person reads belong to whoever is showing them -- the same
    // reason ALXUIEdit::Did says what a step did without saying it in
    // English. A caller applies it as a step like any other, so it appears
    // in the history and comes back off: a fix that cannot be undone is a
    // fix nobody dares press.
    struct Fix
    {
        enum class Do : U8
        {
            Nothing,
            TakeAttributeOut,   // `what` comes off the element
            SpellAttribute,     // `what` comes off and `spelling` goes on carrying its value
            MoveInside,         // the element moves by `dx` and `dy`
            WidenBy             // its width grows by `dx`
        };

        Do          did = Do::Nothing;
        std::string spelling;   // the name the attribute was probably meant to have
        S32         dx = 0;
        S32         dy = 0;
    };

    struct Finding
    {
        Rule                    rule = Rule::ParseError;
        Severity                severity = Severity::Warning;
        ALXUISelection::path_t  path;       // the element, for selecting and badging
        std::string             file;
        S32                     line = 0;
        std::string             what;       // the attribute, tag or name at fault
        // What is wrong, as xui_lint.xml says it in the language in force.
        std::string             message;
        // The same sentence as the name of its line in that file and the
        // names it is about, for anything that wants to say it otherwise:
        // the key names the line, the values fill its brackets. Every key
        // the lint can ask for is in keys().
        std::string             key;
        LLStringUtil::format_map_t args;
        // Empty for most of them: a rule that can say what is wrong cannot
        // always say what right would be.
        Fix                     fix;

        // The fix, done, on the document that writes this element. One step,
        // so one undo puts it back; false where the element or the attribute
        // is not there any anymore, and the document says why.
        //
        // The anchor is what the element occupies now, read from the built
        // view by whoever has one; only the fixes that move or size something
        // look at it.
        bool applyFix(ALXUIEdit& document, const ALXUIEdit::Anchor& now) const;
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
    // The rule a name stands for, for a caller that was handed the name.
    static bool ruleNamed(std::string_view name, Rule& rule);
    // The words. The lint says nothing in English of its own: every
    // sentence it can say is a line in xui_lint.xml, a skin file in a
    // language directory, so it layers and translates the way everything
    // else anybody reads in this viewer does. A finding names its line and
    // the names in it, and the line is read here.
    //
    // Read once, the first time anything asks, from the skin and language
    // in force then: a tool that builds previews under other languages
    // asks before it does, since the file is found through the same
    // directories a preview is.
    static void readWords();
    // A line, filled. Where the file has no line of that name the key is
    // said with the values after it, so nothing arrives blank.
    static std::string wording(std::string_view key, const LLStringUtil::format_map_t& args);
    // The names of the lines for a rule and a severity, and the lines.
    static const char* ruleKey(Rule rule);
    static const char* severityKey(Severity severity);
    static std::string ruleLabel(Rule rule);
    static std::string severityLabel(Severity severity);
    // Every line the lint can ask for, for a test that the file has them.
    static const std::vector<const char*>& keys();

    // The evidence the reference rules and the Bindings inspector share,
    // so both answer from the same place.
    static bool callbackRegistered(const std::string& name);
    static bool controlExists(const std::string& name, std::string* group = nullptr);
    // The widest line of a text that may run to several, in a font: what
    // a box has to be for the text not to be cut.
    static S32 widestLine(const LLFontGL* font, const std::string& text);
    bool fontDeclared(const std::string& name);

private:
    Finding& add(Rule rule, Severity severity, const ALXUISelection::path_t& path,
                 std::string file, S32 line, std::string what, const char* key,
                 LLStringUtil::format_map_t args = LLStringUtil::format_map_t());
    void fromDiagnostics(const Input& input);
    void fromOverlay(const Input& input);
    void walkViews(const Input& input, LLView* view, const ALXUISelection::path_t& path);
    void checkGeometry(const Input& input, LLView* view, const ALXUISelection::path_t& path);
    void checkChildren(const Input& input, LLView* view, const ALXUISelection::path_t& path);
    void checkAttributes(const Input& input, LLView* view, const ALXUISelection::path_t& path,
                         const LLXMLNode* node);
    void checkLayoutDimensions(const Input& input, LLView* view, const ALXUISelection::path_t& path,
                               const LLXMLNode* node);
    void checkCallbacks(const Input& input, const ALXUISelection::path_t& path, S32 line);
    void checkParameterNames(const Input& input, const ALXUISelection::path_t& path, S32 line);

    std::vector<Finding>                        mFindings;
    boost::unordered_map<std::string, S32>      mCountByPath;   // the element and everything below it
    boost::unordered_set<std::string>           mFontFamilies;  // asked for once per run
};
