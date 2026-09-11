/**
 * @file alfloaterxuilibrary.cpp
 * @brief The tags a file may write, what each one is, and one built to look at.
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

#include "alfloaterxuilibrary.h"

#include "alfloaterxuistudio.h"
#include "alstringmatch.h"
#include "alxuicatalog.h"
#include "alxuidiagnostics.h"
#include "alxuinotes.h"
#include "alxuischema.h"
#include "alxuishellbuild.h"
#include "llfiltereditor.h"
#include "llpanel.h"
#include "llscrolllistctrl.h"
#include "lltexteditor.h"
#include "llfloaterreg.h"
#include "lluictrlfactory.h"
#include "llxmlnode.h"

#include <algorithm>
#include <set>

const ALXUICatalog* ALFloaterXUILibrary::catalogOf()
{
    const ALFloaterXUIStudio* studio =
        LLFloaterReg::findTypedInstance<ALFloaterXUIStudio>("xui_studio");
    return studio ? &studio->catalog() : nullptr;
}

ALFloaterXUILibrary::ALFloaterXUILibrary(const LLSD& key)
:   LLFloater(key)
{
}

bool ALFloaterXUILibrary::postBuild()
{
    mFilter = getChild<LLFilterEditor>("library_filter");
    mTags = getChild<LLScrollListCtrl>("library_tags");
    mAbout = getChild<LLTextEditor>("library_about");
    mSpecimen = getChild<LLPanel>("library_specimen");

    mFilter->setCommitCallback(boost::bind(&ALFloaterXUILibrary::onFilter, this));
    mTags->setCommitOnSelectionChange(true);
    mTags->setCommitCallback(boost::bind(&ALFloaterXUILibrary::onTagSelected, this));

    fillTags();
    return true;
}

void ALFloaterXUILibrary::onOpen(const LLSD& key)
{
    // The schema is built the first time anything asks, and a window that
    // has been closed for a while may be opening onto a newer one.
    fillTags();
}

void ALFloaterXUILibrary::onFilter()
{
    fillTags();
}

void ALFloaterXUILibrary::fillTags()
{
    const std::string chosen = mTags->getSelectedValue().asString();
    // Every tag is lower case; what is typed to find one need not be.
    const std::string filter = mFilter ? mFilter->getText() : std::string();
    mTags->deleteAllItems();

    // Grouped, and each group in one place: the list is read down, so a tag
    // is found by the kind of thing it is before it is found by its name.
    const ALXUISchema& schema = ALXUISchema::get();
    std::vector<std::pair<ALXUISchema::Group, std::string>> rows;   // group, tag
    for (const ALXUISchema::Tag& declared : schema.tags())
    {
        if (!ALStringMatch::containsNoCase(declared.name, filter))
        {
            continue;
        }
        rows.emplace_back(ALXUISchema::groupOf(declared.name), declared.name);
    }
    std::sort(rows.begin(), rows.end());

    bool first = true;
    ALXUISchema::Group group = ALXUISchema::Group::Containers;
    for (const auto& [in_group, tag] : rows)
    {
        if (first || in_group != group)
        {
            first = false;
            group = in_group;
            LLSD heading;
            heading["columns"][0]["column"] = "tag";
            heading["columns"][0]["value"] = getString(ALXUISchema::groupKey(group));
            heading["columns"][0]["font"]["style"] = "BOLD";
            LLScrollListItem* item = mTags->addElement(heading);
            item->setEnabled(false);
        }
        LLSD row;
        row["value"] = tag;
        row["columns"][0]["column"] = "tag";
        row["columns"][0]["value"] = "    " + tag;
        mTags->addElement(row);
    }
    // Put back without being chosen again, then described once: choosing
    // it again would describe it, and so would the line after.
    if (!chosen.empty())
    {
        mTags->setCommitOnSelectionChange(false);
        mTags->setSelectedByValue(chosen, true);
        mTags->setCommitOnSelectionChange(true);
    }
    onTagSelected();
}

void ALFloaterXUILibrary::onTagSelected()
{
    const std::string tag = mTags->getSelectedValue().asString();
    mAbout->setText(tag.empty() ? std::string() : describe(tag));
    showSpecimen(tag);
}

std::vector<std::string> ALFloaterXUILibrary::acceptedBy(const std::string& tag) const
{
    std::vector<std::string> takers;
    for (const ALXUISchema::Tag& other : ALXUISchema::get().tags())
    {
        if (other.name != tag && ALXUISchema::get().acceptsChild(other.name, tag))
        {
            takers.push_back(other.name);
        }
    }
    return takers;
}

std::string ALFloaterXUILibrary::describe(const std::string& tag) const
{
    const ALXUISchema::Tag* declared = ALXUISchema::get().tag(tag);
    LLStringUtil::format_map_t args;
    args["[TAG]"] = tag;
    if (!declared)
    {
        return getString("LibraryUnknownTag", args);
    }

    // Each part in the floater's words, with the counts and names put in.
    const auto said = [&](const char* name, const std::string& value = LLStringUtil::null)
    {
        LLStringUtil::format_map_t with(args);
        with["[VALUE]"] = value;
        return getString(name, with);
    };
    const auto counted = [&](const char* name, size_t count)
    {
        return said(name, std::to_string(count));
    };

    std::string out = "<" + tag + ">\n";
    // What it is for, which is the first thing anybody asks of a vocabulary
    // this size and the one thing a registry cannot answer. A tag nobody has
    // written a line for simply has one fewer line.
    if (const std::string& note = ALXUINotes::get().note(tag); !note.empty())
    {
        out += "\n" + note + "\n";
    }
    if (declared->text)
    {
        out += "\n" + said("LibraryTakesText") + "\n";
    }

    out += "\n" + counted("LibraryAttributes", declared->attributes.size()) + "\n";
    for (const ALXUISchema::Attribute& a : declared->attributes)
    {
        out += "    " + a.name;
        if (!a.type.empty())
        {
            out += " : " + a.type;
        }
        if (a.required)
        {
            out += "   " + said("LibraryRequired");
        }
        if (a.ignored)
        {
            out += "   " + said("LibraryIgnored");
        }
        // A name that works and should not be used, and the one to use
        // instead. Both of them go on working: what is said here is which of
        // them anything new should be written with.
        if (a.deprecated)
        {
            out += "   " + (a.instead.empty() ? said("LibraryDeprecated")
                                              : said("LibraryDeprecatedFor", a.instead));
        }
        else if (!a.alias.empty())
        {
            out += "   " + said("LibraryAlias", a.alias);
        }
        if (!a.values.empty())
        {
            out += "\n        " + said("LibraryOneOf") + " ";
            for (size_t i = 0; i < a.values.size(); ++i)
            {
                out += (i ? ", " : "") + a.values[i];
            }
        }
        out += "\n";
    }

    if (!declared->elements.empty())
    {
        out += "\n" + counted("LibraryElements", declared->elements.size()) + "\n";
        for (const ALXUISchema::Element& e : declared->elements)
        {
            out += "    <" + e.name + ">";
            if (e.maxCount > 1)
            {
                out += "   " + said("LibraryAnyNumber");
            }
            if (e.minCount > 0)
            {
                out += "   " + said("LibraryRequired");
            }
            out += "\n";
        }
    }

    out += "\n" + counted("LibraryMayContain", declared->children.size()) + "\n";
    if (declared->children.empty())
    {
        out += "    " + said("LibraryNothing") + "\n";
    }
    else
    {
        out += "   ";
        for (size_t i = 0; i < declared->children.size(); ++i)
        {
            out += (i ? ", " : " ") + declared->children[i];
        }
        out += "\n";
    }

    // The question the tool could not answer before this: a developer with a
    // tag in hand wants to know where it is allowed to go.
    const std::vector<std::string> takers = acceptedBy(tag);
    out += "\n" + counted("LibraryMayGoInside", takers.size()) + "\n";
    if (takers.empty())
    {
        out += "    " + said("LibraryRoot") + "\n";
    }
    else
    {
        out += "   ";
        for (size_t i = 0; i < takers.size(); ++i)
        {
            out += (i ? ", " : " ") + takers[i];
        }
        out += "\n";
    }

    // The honest answer to "is this thing still used". The studio has read
    // the whole skin already, so this asks it rather than reading it again;
    // with the studio closed there is nothing to count and the line is left
    // off rather than guessed at.
    if (const ALXUICatalog* catalog = ALFloaterXUILibrary::catalogOf())
    {
        std::set<const ALXUICatalog::Entry*> files;
        for (const ALXUICatalog::Hit& hit : catalog->find(tag, ALXUICatalog::Field::Tag,
                                                          ALXUICatalog::Match::Matching))
        {
            files.insert(hit.entry);
        }
        out += "\n" + counted(files.size() == 1 ? "LibraryUsedInOne" : "LibraryUsedIn", files.size()) + "\n";
    }

    // Real, valid and pasteable: the attributes every element needs and
    // nothing the tag would ignore.
    out += "\n" + said("LibraryToWrite") + "\n    <" + tag + " name=\"\" layout=\"topleft\""
           " left=\"0\" top=\"0\" width=\"100\" height=\"20\"";
    out += declared->text ? ">text</" + tag + ">\n" : "/>\n";
    return out;
}

void ALFloaterXUILibrary::showSpecimen(const std::string& tag)
{
    mSpecimen->deleteAllChildren();
    if (tag.empty() || !ALXUISchema::buildsAlone(tag))
    {
        return;
    }

    // Built the way the file would write it, with nothing but a name, a
    // label and a size. Diagnostics are swallowed: a tag that will not build
    // from its defaults is a blank space here, not a report -- and not a
    // warning in the log for every tag that takes no label.
    ALXUIShellBuild shell;
    ALXUIDiagnostics sink;
    const LLRect room = mSpecimen->getLocalRect();
    const std::string xml = "<" + tag + " name=\"specimen\" label=\"" + tag
        + "\" layout=\"topleft\" left=\"8\" top=\"8\" width=\""
        + std::to_string(llmax(60, room.getWidth() - 16)) + "\" height=\"24\"/>";
    LLXMLNodePtr node;
    if (!LLXMLNode::parseBuffer(xml.data(), xml.size(), node))
    {
        return;
    }
    LLUICtrlFactory& factory = LLUICtrlFactory::instance();
    factory.pushFileName("library");
    factory.createFromXML(node, mSpecimen, "library", LLDefaultChildRegistry::instance());
    factory.popFileName();
}
