/**
 * @file alcolortablepanel.cpp
 * @brief The colour table as rows to edit: every named colour, whose it is, and the way back
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

#include "alcolortablepanel.h"

#include "alcolorfield.h"
#include "alpropertygrid.h"
#include "llcheckboxctrl.h"
#include "llfiltereditor.h"
#include "lltrans.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <fmt/format.h>

#include <algorithm>

#include <boost/unordered/unordered_flat_map.hpp>

static LLDefaultChildRegistry::Register<ALColorTablePanel> r("color_table_panel");

namespace
{
    constexpr S32 ROW = 22;
    constexpr S32 GAP = 4;
    // How long after the last change the colours are written: a slider
    // dragged is one write, not one per pixel.
    constexpr F32 WRITE_AFTER_SECONDS = 2.f;

    // What a file is called on a heading: the skin's folder and the file,
    // which is what tells default/colors.xml from alchemy/colors.xml.
    std::string headingFor(const std::string& path)
    {
        std::string named = path;
        std::replace(named.begin(), named.end(), '\\', '/');
        const size_t slash = named.rfind('/');
        const size_t before = slash == std::string::npos ? std::string::npos : named.rfind('/', slash - 1);
        return before == std::string::npos ? named : named.substr(before + 1);
    }
}

ALColorTablePanel::Params::Params()
:   label_width("label_width", 180)
{
}

ALColorTablePanel::ALColorTablePanel(const Params& p)
:   LLPanel(p)
{
    const S32 width = getRect().getWidth();
    const S32 height = getRect().getHeight();

    LLFilterEditor::Params fp;
    fp.name = "color_filter";
    fp.rect = LLRect(0, height, width, height - ROW);
    fp.label = LLTrans::getString("ColorTableFilter");
    fp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
    mFilter = LLUICtrlFactory::create<LLFilterEditor>(fp);
    mFilter->setCommitCallback([this](LLUICtrl* ctrl, const LLSD&) { setFilter(ctrl->getValue().asString()); });
    addChild(mFilter);

    LLCheckBoxCtrl::Params cp;
    cp.name = "only_changed";
    cp.rect = LLRect(0, height - ROW - GAP, width, height - ROW - GAP - 18);
    cp.label = LLTrans::getString("ColorTableOnlyChanged");
    cp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
    mOnlyChanged = LLUICtrlFactory::create<LLCheckBoxCtrl>(cp);
    mOnlyChanged->setCommitCallback([this](LLUICtrl* ctrl, const LLSD&) { setOnlyChanged(ctrl->getValue().asBoolean()); });
    addChild(mOnlyChanged);

    ALPropertyGrid::Params gp;
    gp.name = "color_grid";
    gp.rect = LLRect(0, height - ROW - GAP - 18 - GAP, width, 0);
    gp.label_width = p.label_width;
    gp.follows.flags = FOLLOWS_ALL;
    mGrid = LLUICtrlFactory::create<ALPropertyGrid>(gp);

    ALPropertyGrid::Tips tips;
    tips.field = LLTrans::getString("ColorTableTipField");
    tips.fieldTyped = LLTrans::getString("ColorTableTipField");
    tips.source = LLTrans::getString("ColorTableTipSource");
    tips.unwritten = LLTrans::getString("ColorTableTipUnwritten");
    tips.remove = LLTrans::getString("ColorTableTipRemove");
    mGrid->setTips(tips);
    mGrid->setNotices({ LLTrans::getString("ColorTableNothing"), LLTrans::getString("ColorTableNothingHow"), "" },
                      { LLTrans::getString("ColorTableNothing"), LLTrans::getString("ColorTableNothingHow"), "" },
                      { LLTrans::getString("ColorTableNoMatch"), LLTrans::getString("ColorTableNoMatchHow"), "" });
    // Every colour is one the skin chose, so no section arrives folded.
    mGrid->setFoldsUnwritten(false);
    mGrid->onFieldCommit([this](const std::string& name, const std::string& value) { onFieldCommit(name, value); });
    mGrid->onFieldRemove([this](const std::string& name) { onFieldRemove(name); });
    mGrid->onFieldGutter([this](const std::string& name) { mGutter(name); });
    addChild(mGrid);

    fill();
}

void ALColorTablePanel::setUsers(users_t users)
{
    mUsers = std::move(users);
    fill();
}

LLView* ALColorTablePanel::gutterFor(const std::string& name) const
{
    return mGrid->gutterFor(name);
}

std::string ALColorTablePanel::valueText(const LLColor4& color)
{
    return fmt::format("{:g}, {:g}, {:g}, {:g}", color.mV[0], color.mV[1], color.mV[2], color.mV[3]);
}

void ALColorTablePanel::refresh()
{
    fill();
}

void ALColorTablePanel::setFilter(const std::string& text)
{
    if (mFilter->getValue().asString() != text)
    {
        mFilter->setValue(text);
    }
    mGrid->setFilter(text);
}

void ALColorTablePanel::setOnlyChanged(bool only)
{
    if (mOnlyChanged->getValue().asBoolean() != only)
    {
        mOnlyChanged->setValue(only);
    }
    mGrid->setAuthoredOnly(only);
}

bool ALColorTablePanel::onlyChanged() const
{
    return mGrid->authoredOnly();
}

void ALColorTablePanel::showColor(const std::string& name)
{
    setOnlyChanged(false);
    setFilter(name);
}

// The table read again once it has changed under the rows: the same rows
// take the new values in place, so a colour being picked keeps its
// popover.
void ALColorTablePanel::draw()
{
    if (mSeenGeneration != LLUIColorTable::instance().generation())
    {
        fill();
        mUnwritten = true;
        mSinceChange.reset();
    }
    if (mUnwritten && mSinceChange.getElapsedTimeF32() > WRITE_AFTER_SECONDS)
    {
        mUnwritten = false;
        LLUIColorTable::instance().saveUserSettings();
    }
    LLPanel::draw();
}

// One row per name, under the file that declares it, with the changed
// ones under a heading of their own in front: what a person did to the
// theme reads as a list, and what the skin did reads by file.
void ALColorTablePanel::fill()
{
    const LLUIColorTable& table = LLUIColorTable::instance();
    const ALColorSheet& sheet = table.getLoadedSheet();

    mSeenGeneration = table.generation();

    std::vector<std::string> files;
    for (const std::string& file : sheet.files())
    {
        files.push_back(headingFor(file));
    }
    if (files != mFiles)
    {
        mFiles = files;
        std::vector<std::string> groups = { LLTrans::getString("ColorTableGroupChanged") };
        groups.insert(groups.end(), files.begin(), files.end());
        groups.push_back(LLTrans::getString("ColorTableGroupAdded"));
        mGrid->setGroups(std::move(groups));
    }
    const S32 added_group = S32(mFiles.size()) + 1;

    // What the sheet had to say about a name, for the row's sentence.
    boost::unordered_flat_map<std::string, std::string> complaints;
    for (const ALColorSheet::Diagnostic& diagnostic : sheet.diagnostics())
    {
        if (diagnostic.declaration >= 0)
        {
            const std::string& name = sheet.declarations()[diagnostic.declaration].name;
            complaints[name] += (complaints[name].empty() ? "" : "\n") + diagnostic.message;
        }
    }

    std::vector<ALPropertyGrid::Field> fields;
    const auto row = [&](const std::string& name, const LLColor4& color)
    {
        ALPropertyGrid::Field field;
        field.name = name;
        field.value = valueText(color);
        field.type = "LLColor4";
        field.authored = !table.isDefault(name);
        field.group = added_group;

        if (const ALColorSheet::Declaration* declared = sheet.declarationOf(name))
        {
            field.source = fmt::format("{}({})", headingFor(std::string(sheet.fileOf(*declared))), declared->line);
            field.group = S32(declared->file) + 1;
            if (declared->kind == ALColorSheet::Declaration::Kind::Reference)
            {
                field.description = LLTrans::getString("ColorTableRefersTo", LLStringUtil::format_map_t{ { "[NAME]", declared->reference } });
            }
        }
        if (field.authored)
        {
            field.group = 0;
            if (const LLColor4* skins = sheet.find(name))
            {
                const std::string was = LLTrans::getString("ColorTableWas", LLStringUtil::format_map_t{ { "[VALUE]", valueText(*skins) } });
                field.description += (field.description.empty() ? "" : "\n") + was;
            }
        }
        if (const auto complaint = complaints.find(name); complaint != complaints.end())
        {
            field.description += (field.description.empty() ? "" : "\n") + complaint->second;
        }
        if (mUsers)
        {
            // The shapes painted with it, counted in the sentence and
            // named on the mark.
            field.alsoWritten = mUsers(name);
            const std::string count = field.alsoWritten.empty()
                ? LLTrans::getString("ColorTableUnused")
                : LLTrans::getString("ColorTableUsers", LLStringUtil::format_map_t{ { "[COUNT]", std::to_string(field.alsoWritten.size()) } });
            field.description += (field.description.empty() ? "" : "\n") + count;
        }
        fields.push_back(std::move(field));
    };

    for (const auto& [name, color] : table.getLoadedColors())
    {
        row(name, table.getColor(name).get());
    }
    for (const auto& [name, color] : table.getUserColors())
    {
        if (!table.getLoadedColors().contains(name))
        {
            row(name, color.get());
        }
    }

    if (!mGrid->updateFields(fields))
    {
        mGrid->setFields(std::move(fields));
    }
}

void ALColorTablePanel::onFieldCommit(const std::string& name, const std::string& value)
{
    LLColor4 color;
    if (!ALColorField::textToColor(value, color))
    {
        return;
    }
    LLUIColorTable::instance().setColor(name, color);
    mChanged(name);
}

void ALColorTablePanel::onFieldRemove(const std::string& name)
{
    LLUIColorTable::instance().resetToDefault(name);
    mChanged(name);
}
