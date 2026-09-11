/**
 * @file alxuibaseline.cpp
 * @brief Builds every shipped XUI file and counts what goes wrong.
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

#include "llviewerprecompiledheaders.h"

#include "alxuibaseline.h"

#include "alxmldocument.h"
#include "alxuicatalog.h"
#include "alxuidiagnostics.h"
#include "alxuilint.h"
#include "alxuishellbuild.h"
#include "llbutton.h"
#include "lldir.h"
#include "lldiriterator.h"
#include "lldraghandle.h"
#include "llfile.h"
#include "lllayoutstack.h"
#include "llpanel.h"
#include "llresizebar.h"
#include "lltextbox.h"
#include "lltimer.h"
#include "lluictrlfactory.h"
#include "llviewborder.h"

#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace
{
    // Every registry a child tag can be built from. A tag that none of them
    // knows is either a parameter element or a mistake.
    bool isWidgetTag(const std::string& tag)
    {
        return ALXUICatalog::isWidgetTag(tag);
    }

    std::string firstToken(const std::string& path)
    {
        const size_t dot = path.find('.');
        return dot == std::string::npos ? path : path.substr(0, dot);
    }

    bool overlapIgnorable(const LLView* view)
    {
        return view->as<LLDragHandle>() || view->as<LLViewBorder>() || view->as<LLResizeBar>();
    }

    bool isContainer(const LLView* view)
    {
        return view->as<LLPanel>() || view->as<LLLayoutStack>();
    }

    // The old tool's check: two sibling rects that intersect by more than a
    // couple of pixels, visible or not, since a hidden tab is shown later.
    bool rectsOverlap(const LLRect& a, const LLRect& b)
    {
        constexpr S32 tolerance = 2;
        return a.mLeft <= b.mRight - tolerance
            && b.mLeft <= a.mRight - tolerance
            && a.mBottom <= b.mTop - tolerance
            && b.mBottom <= a.mTop - tolerance;
    }

    void countOverlaps(const LLView* parent, S32& pairs)
    {
        if (!isContainer(parent))
        {
            return;
        }
        const LLView::child_list_t& children = *parent->getChildList();
        for (auto it = children.begin(); it != children.end(); ++it)
        {
            const LLView* child = *it;
            if (overlapIgnorable(child))
            {
                continue;
            }
            for (auto jt = std::next(it); jt != children.end(); ++jt)
            {
                const LLView* sibling = *jt;
                if (!overlapIgnorable(sibling) && rectsOverlap(child->getRect(), sibling->getRect()))
                {
                    ++pairs;
                }
            }
            countOverlaps(child, pairs);
        }
    }

    // A label wider than the box it sits in. Text boxes that wrap are
    // skipped; buttons allow the default horizontal padding on each side.
    // A view whose name the file never mentions was built by a widget's
    // own code, and is counted apart: the file cannot fix it.
    void countTruncation(const LLView* view, const std::set<std::string>& xml_names,
                         S32& count, S32& code_built, std::vector<std::string>& names)
    {
        std::string text;
        S32 needed = 0;
        S32 room = 0;
        const bool truncated = ALXUILint::measuresText(view, text, needed, room) && needed > room;
        if (truncated)
        {
            if (xml_names.count(view->getName()))
            {
                ++count;
                names.push_back(view->getName());
            }
            else
            {
                ++code_built;
            }
        }
        for (const LLView* child : *view->getChildList())
        {
            countTruncation(child, xml_names, count, code_built, names);
        }
    }

    void collectNames(const pugi::xml_node& node, std::set<std::string>& names)
    {
        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
        {
            if (child.type() != pugi::node_element)
            {
                continue;
            }
            if (const char* name = child.attribute("name").as_string(nullptr))
            {
                names.insert(name);
            }
            collectNames(child, names);
        }
    }

    S32 countViews(const LLView* view)
    {
        S32 n = 1;
        for (const LLView* child : *view->getChildList())
        {
            n += countViews(child);
        }
        return n;
    }

    void tallyTags(const pugi::xml_node& node, std::map<std::string, S32>& tags)
    {
        for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling())
        {
            if (child.type() != pugi::node_element)
            {
                continue;
            }
            ++tags[child.name()];
            tallyTags(child, tags);
        }
    }

    struct FileResult
    {
        std::string name;
        std::string root;
        bool        built = false;
        S32         views = 0;
        S32         overlaps = 0;
        S32         truncated = 0;
        S32         truncatedCodeBuilt = 0;
        S32         shown = 0;          // failures the viewer logs today: depth 0 attributes, factory refusals
        S32         hidden = 0;         // failures it keeps quiet about: nested attributes, mis-scoped elements
        S32         childNoise = 0;     // a child widget's attributes failing against the parent, by design
        S32         errors = 0;
        F32         seconds = 0.f;
        std::vector<std::string> truncatedNames;
    };

    std::string rootTag(const std::string& path, std::set<std::string>& xml_names)
    {
        ALXmlDocument doc;
        if (!doc.loadFile(path))
        {
            return std::string();
        }
        const pugi::xml_node root = doc.document().document_element();
        if (const char* name = root.attribute("name").as_string(nullptr))
        {
            xml_names.insert(name);
        }
        collectNames(root, xml_names);
        return root.name();
    }

    struct BaselineFloater : public LLFloater
    {
        BaselineFloater(const LLFloater::Params& p) : LLFloater(LLSD(), p) {}
    };

    // A floater is not a child tag, so the factory cannot build one from
    // its root; the viewer goes through buildFromFile, and so does this.
    // Everything else is a registered tag and builds under the factory's
    // dummy panel.
    LLView* build(const std::string& root, const std::string& name)
    {
        if (root == "floater" || root == "multi_floater")
        {
            LLFloater* floater = new BaselineFloater(LLFloater::getDefaultParams());
            if (!floater->buildFromFile(name))
            {
                delete floater;
                return nullptr;
            }
            return floater;
        }
        return LLUICtrlFactory::getInstance()->createFromFile<LLView>(name, nullptr, LLDefaultChildRegistry::instance());
    }
}

void ALXUIBaseline::run()
{
    const std::string delim = gDirUtilp->getDirDelimiter();
    const std::string xui_dir = gDirUtilp->getDefaultSkinDir() + delim + "xui" + delim + "en" + delim;
    const std::string widgets_dir = xui_dir + "widgets" + delim;

    std::vector<std::string> files;
    {
        LLDirIterator it(xui_dir, "*.xml");
        std::string name;
        while (it.next(name))
        {
            files.push_back(name);
        }
    }
    std::sort(files.begin(), files.end());

    std::vector<std::string> templates;
    {
        LLDirIterator it(widgets_dir, "*.xml");
        std::string name;
        while (it.next(name))
        {
            templates.push_back(name);
        }
    }

    // Every element tag in every file, against the registries.
    std::map<std::string, S32> tags;
    for (const std::string& name : files)
    {
        ALXmlDocument doc;
        if (doc.loadFile(xui_dir + name))
        {
            ++tags[doc.document().document_element().name()];
            tallyTags(doc.document().document_element(), tags);
        }
    }
    for (const std::string& name : templates)
    {
        ALXmlDocument doc;
        if (doc.loadFile(widgets_dir + name))
        {
            ++tags[doc.document().document_element().name()];
            tallyTags(doc.document().document_element(), tags);
        }
    }

    std::vector<FileResult> results;
    results.reserve(files.size());
    std::map<std::string, S32> kinds;
    std::map<std::string, S32> kindsBuilt;
    std::map<std::string, S32> kindsFailed;
    S32 totalShown = 0, totalHidden = 0, totalNoise = 0, totalErrors = 0;
    S32 totalOverlaps = 0, totalTruncated = 0, totalTruncatedCodeBuilt = 0, totalViews = 0;
    S32 filesWithOverlaps = 0, filesWithTruncation = 0;
    std::map<std::string, S32> hiddenByPath;
    std::vector<std::string> detail;

    LL_INFOS("XUIBaseline") << "building " << files.size() << " files from " << xui_dir << LL_ENDL;

    // Shells only: a registered panel class is viewer code that expects to
    // exist once, and the session already has its one.
    ALXUIShellBuild shell;
    ALXUIDiagnostics sink;
    for (const std::string& name : files)
    {
        FileResult r;
        r.name = name;
        std::set<std::string> xml_names;
        r.root = rootTag(xui_dir + name, xml_names);
        // A floater is not a child tag, so it is not in any registry; it
        // is built all the same, through its own path in build().
        std::string kind = r.root;
        if (kind == "context_menu" || kind == "menu_bar" || kind == "toggleable_menu")
        {
            kind = "menu";
        }
        else if (kind == "multi_floater")
        {
            kind = "floater";
        }
        else if (kind != "floater" && !isWidgetTag(kind))
        {
            kind = "not a widget";
        }
        ++kinds[kind];

        if (kind == "not a widget")
        {
            results.push_back(r);
            continue;
        }

        // Named before it is built, so a crash names its file.
        LL_INFOS("XUIBaseline") << "building " << name << LL_ENDL;
        sink.clear();
        LLTimer timer;
        LLView* view = build(r.root, name);
        r.seconds = timer.getElapsedTimeF32();
        r.built = view != nullptr;
        (r.built ? kindsBuilt : kindsFailed)[kind]++;

        for (const ALXUIDiagnostics::Entry& e : sink.entries())
        {
            switch (e.kind)
            {
            case ALXUIDiagnostics::Kind::ParseError:
                ++r.errors;
                break;
            case ALXUIDiagnostics::Kind::ParseWarning:
            case ALXUIDiagnostics::Kind::InvalidChild:
            case ALXUIDiagnostics::Kind::CreateFailed:
                ++r.shown;
                break;
            case ALXUIDiagnostics::Kind::UnknownAttribute:
                if (e.depth == 0)
                {
                    ++r.shown;
                }
                else if (isWidgetTag(firstToken(e.path)))
                {
                    ++r.childNoise;
                }
                else
                {
                    ++r.hidden;
                    ++hiddenByPath[e.path];
                }
                break;
            case ALXUIDiagnostics::Kind::MisScopedElement:
                ++r.hidden;
                ++hiddenByPath[e.path];
                break;
            }
            if (e.kind != ALXUIDiagnostics::Kind::UnknownAttribute || e.depth == 0 || !isWidgetTag(firstToken(e.path)))
            {
                std::ostringstream line;
                line << "  " << ALXUIDiagnostics::kindName(e.kind) << " depth " << e.depth
                     << " " << e.file << ":" << e.line << " " << e.path;
                if (!e.message.empty())
                {
                    line << " " << e.message;
                }
                detail.push_back(line.str());
            }
        }

        if (view)
        {
            r.views = countViews(view);
            countOverlaps(view, r.overlaps);
            countTruncation(view, xml_names, r.truncated, r.truncatedCodeBuilt, r.truncatedNames);
            deleteView(view);
        }

        totalShown += r.shown;
        totalHidden += r.hidden;
        totalNoise += r.childNoise;
        totalErrors += r.errors;
        totalOverlaps += r.overlaps;
        totalTruncated += r.truncated;
        totalTruncatedCodeBuilt += r.truncatedCodeBuilt;
        totalViews += r.views;
        filesWithOverlaps += r.overlaps > 0;
        filesWithTruncation += r.truncated > 0;
        results.push_back(r);
    }

    S32 widgetTags = 0, dottedTags = 0, otherTags = 0;
    std::map<std::string, S32> unregistered;
    for (const auto& [tag, n] : tags)
    {
        if (isWidgetTag(tag))
        {
            ++widgetTags;
        }
        else if (tag.find('.') != std::string::npos)
        {
            ++dottedTags;
        }
        else
        {
            ++otherTags;
            unregistered[tag] = n;
        }
    }

    std::ostringstream out;
    out << "XUI baseline: " << xui_dir << "\n";
    out << "skin " << gDirUtilp->getSkinFolder() << ", language " << gDirUtilp->getLanguage() << "\n\n";
    out << "files " << files.size() << ", widget templates " << templates.size() << "\n";
    out << std::left << std::setw(16) << "kind" << std::right << std::setw(8) << "files" << std::setw(8) << "built" << std::setw(8) << "failed\n";
    for (const auto& [kind, n] : kinds)
    {
        out << std::left << std::setw(16) << kind << std::right << std::setw(8) << n
            << std::setw(8) << kindsBuilt[kind] << std::setw(8) << kindsFailed[kind] << "\n";
    }
    out << "\nviews built " << totalViews << "\n";
    out << "parse errors " << totalErrors << "\n";
    out << "failures the log shows today (depth 0 attributes, factory refusals) " << totalShown << "\n";
    out << "failures it hides (nested attributes, mis-scoped elements) " << totalHidden << "\n";
    out << "child widget attributes failing against the parent, by design " << totalNoise << "\n";
    out << "overlapping sibling pairs " << totalOverlaps << " in " << filesWithOverlaps << " files\n";
    out << "truncated labels " << totalTruncated << " in " << filesWithTruncation << " files, plus "
        << totalTruncatedCodeBuilt << " on children a widget built for itself\n";
    out << "\ntags: " << tags.size() << " distinct, " << widgetTags << " registered widgets, "
        << dottedTags << " dotted parameter elements, " << otherTags << " registered nowhere:\n";
    for (const auto& [tag, n] : unregistered)
    {
        out << "  " << std::left << std::setw(32) << tag << std::right << std::setw(6) << n << "\n";
    }
    out << "\nhidden failures by path:\n";
    for (const auto& [path, n] : hiddenByPath)
    {
        out << "  " << std::left << std::setw(48) << path << std::right << std::setw(6) << n << "\n";
    }

    LL_INFOS("XUIBaseline") << "\n" << out.str() << LL_ENDL;

    out << "\nper file: name root built views overlaps truncated truncated_code_built shown hidden noise errors seconds\n";
    for (const FileResult& r : results)
    {
        out << r.name << " " << r.root << " " << (r.built ? "built" : "-") << " " << r.views << " "
            << r.overlaps << " " << r.truncated << " " << r.truncatedCodeBuilt << " " << r.shown << " " << r.hidden << " "
            << r.childNoise << " " << r.errors << " " << std::fixed << std::setprecision(3) << r.seconds << "\n";
        for (const std::string& n : r.truncatedNames)
        {
            out << "  truncated " << n << "\n";
        }
    }
    out << "\ndiagnostics:\n";
    for (const std::string& line : detail)
    {
        out << line << "\n";
    }

    const std::string report_path = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "xui_baseline.txt");
    llofstream report(report_path);
    if (report.is_open())
    {
        report << out.str();
        LL_INFOS("XUIBaseline") << "wrote " << report_path << LL_ENDL;
    }
    else
    {
        LL_WARNS("XUIBaseline") << "could not write " << report_path << LL_ENDL;
    }
}
