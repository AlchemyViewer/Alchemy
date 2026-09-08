/**
 * @file alcolorfield.cpp
 * @brief A colour as a XUI file writes one: a swatch that opens the names.
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

#include "alcolorfield.h"

#include "alcolorpicker.h"

#include "llfiltereditor.h"
#include "llfloater.h"
#include "llfocusmgr.h"
#include "lllineeditor.h"
#include "llrender2dutils.h"
#include "llscrollcontainer.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"
#include "lluicolortable.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALColorField> r("color_field");

namespace
{
    constexpr S32 SWATCH = 18;
    constexpr S32 GAP = 3;
    constexpr S32 PER_ROW = 12;
    constexpr S32 POPOVER_WIDTH = PER_ROW * (SWATCH + GAP) + GAP + 8;
    constexpr S32 GRID_HEIGHT = 320;

    // Four numbers, which is the other thing a file writes for a colour.
    bool literalColor(const std::string& text, LLColor4& out)
    {
        F32 v[4] = { 0.f, 0.f, 0.f, 1.f };
        S32 found = 0;
        const char* p = text.c_str();
        while (*p && found < 4)
        {
            while (*p == ' ' || *p == ',' || *p == '\t') { ++p; }
            if (!*p) { break; }
            char* end = nullptr;
            const F32 value = (F32)std::strtod(p, &end);
            if (end == p) { return false; }
            v[found++] = value;
            p = end;
        }
        if (found < 3)
        {
            return false;
        }
        out.set(v[0], v[1], v[2], v[3]);
        return true;
    }

    // The swatches themselves. A grid rather than a list because a colour
    // is recognised and not read: forty of them fit where four names would.
    class ALColorSwatchGrid final : public LLPanel
    {
    public:
        AL_VIEW_TYPE(ALColorSwatchGrid, LLPanel);

        typedef std::function<void(const std::string&)> chose_t;

        struct Entry
        {
            std::string name;
            LLColor4    color;
        };

        ALColorSwatchGrid(const LLPanel::Params& p, chose_t chose)
        :   LLPanel(p),
            mChose(std::move(chose))
        {
            for (const auto& [name, color] : LLUIColorTable::instance().getLoadedColors())
            {
                mAll.push_back({ name, color.get() });
            }
            std::sort(mAll.begin(), mAll.end(),
                      [](const Entry& a, const Entry& b) { return a.name < b.name; });
            filter(LLStringUtil::null);
        }

        void filter(const std::string& text)
        {
            std::string wanted(text);
            LLStringUtil::toLower(wanted);
            mShown.clear();
            for (const Entry& entry : mAll)
            {
                if (wanted.empty())
                {
                    mShown.push_back(&entry);
                    continue;
                }
                std::string name(entry.name);
                LLStringUtil::toLower(name);
                if (name.find(wanted) != std::string::npos)
                {
                    mShown.push_back(&entry);
                }
            }
            const S32 rows = ((S32)mShown.size() + PER_ROW - 1) / PER_ROW;
            reshape(getRect().getWidth(), llmax(1, rows) * (SWATCH + GAP) + GAP, false);
            mHover = -1;
        }

        void draw() override
        {
            LLPanel::draw();
            static const LLUIColor edge = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
            for (size_t i = 0; i < mShown.size(); ++i)
            {
                const LLRect cell = rectOf((S32)i);
                gl_rect_2d(cell, mShown[i]->color, true);
                gl_rect_2d(cell, (S32)i == mHover ? LLColor4::white : edge.get(), false);
            }
        }

        bool handleHover(S32 x, S32 y, MASK mask) override
        {
            mHover = at(x, y);
            if (mHover >= 0)
            {
                setToolTip(mShown[mHover]->name);
            }
            return LLPanel::handleHover(x, y, mask);
        }

        bool handleMouseDown(S32 x, S32 y, MASK mask) override
        {
            const S32 which = at(x, y);
            if (which >= 0)
            {
                mChose(mShown[which]->name);
                return true;
            }
            return LLPanel::handleMouseDown(x, y, mask);
        }

    private:
        LLRect rectOf(S32 index) const
        {
            const S32 column = index % PER_ROW;
            const S32 row = index / PER_ROW;
            const S32 left = GAP + column * (SWATCH + GAP);
            const S32 top = getRect().getHeight() - GAP - row * (SWATCH + GAP);
            return LLRect(left, top, left + SWATCH, top - SWATCH);
        }

        S32 at(S32 x, S32 y) const
        {
            for (size_t i = 0; i < mShown.size(); ++i)
            {
                if (rectOf((S32)i).pointInRect(x, y))
                {
                    return (S32)i;
                }
            }
            return -1;
        }

        std::vector<Entry>          mAll;
        std::vector<const Entry*>   mShown;
        chose_t                     mChose;
        S32                         mHover = -1;
    };

    // The popover: a floater with no chrome, closed by choosing, by
    // clicking away or by escape. A floater rather than a child of the
    // field, because a field in a scroll container has nowhere to drop a
    // list of four hundred colours.
    class ALColorPopover final : public LLFloater
    {
    public:
        AL_VIEW_TYPE(ALColorPopover, LLFloater);

        ALColorPopover(const LLFloater::Params& p, ALColorSwatchGrid::chose_t chose)
        :   LLFloater(LLSD(), p)
        {
            LLFilterEditor::Params fp;
            fp.name = "filter";
            fp.rect = LLRect(4, GRID_HEIGHT + 26, POPOVER_WIDTH - 4, GRID_HEIGHT + 4);
            fp.label = "Filter";
            mFilter = LLUICtrlFactory::create<LLFilterEditor>(fp);
            addChild(mFilter);

            LLTabContainer::Params tp;
            tp.name = "tabs";
            tp.rect = LLRect(2, GRID_HEIGHT, POPOVER_WIDTH - 2, 2);
            tp.tab_position = LLTabContainer::TOP;
            tp.tab_height = 20;
            LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(tp);
            addChild(tabs);

            LLPanel::Params np;
            np.name = "names";
            np.label = "Names";
            np.rect = LLRect(0, GRID_HEIGHT - 20, POPOVER_WIDTH - 6, 0);
            LLPanel* names = LLUICtrlFactory::create<LLPanel>(np);

            LLScrollContainer::Params sp;
            sp.name = "scroll";
            sp.rect = names->getRect();
            sp.follows.flags = FOLLOWS_ALL;
            LLScrollContainer* scroll = LLUICtrlFactory::create<LLScrollContainer>(sp);
            names->addChild(scroll);

            LLPanel::Params gp;
            gp.name = "swatches";
            gp.rect = LLRect(0, GRID_HEIGHT - 20, POPOVER_WIDTH - 26, 0);
            gp.background_visible = false;
            mGrid = new ALColorSwatchGrid(gp, chose);
            scroll->addChild(mGrid);
            tabs->addTabPanel(names);

            // The other way a file writes a colour, for the times the
            // table has no name for what is wanted.
            LLPanel::Params cp;
            cp.name = "custom";
            cp.label = "Custom";
            cp.rect = LLRect(0, GRID_HEIGHT - 20, POPOVER_WIDTH - 6, 0);
            LLPanel* custom = LLUICtrlFactory::create<LLPanel>(cp);

            ALColorPicker::Params pp;
            pp.name = "picker";
            pp.rect = LLRect(2, GRID_HEIGHT - 22, POPOVER_WIDTH - 8, 2);
            pp.follows.flags = FOLLOWS_ALL;
            ALColorPicker* picker = LLUICtrlFactory::create<ALColorPicker>(pp);
            picker->setCommitCallback([chose, picker](LLUICtrl*, const LLSD&)
            {
                chose(picker->getValue().asString());
            });
            custom->addChild(picker);
            tabs->addTabPanel(custom);

            mFilter->setCommitCallback([this](LLUICtrl*, const LLSD&)
            {
                mGrid->filter(mFilter->getText());
            });
        }

        void onFocusLost() override
        {
            closeFloater();
        }

        bool handleKeyHere(KEY key, MASK mask) override
        {
            if (key == KEY_ESCAPE && mask == MASK_NONE)
            {
                closeFloater();
                return true;
            }
            return LLFloater::handleKeyHere(key, mask);
        }

    private:
        LLFilterEditor*     mFilter = nullptr;
        ALColorSwatchGrid*  mGrid = nullptr;
    };
}

ALColorField::Params::Params()
:   swatch_width("swatch_width", 22)
{
}

ALColorField::ALColorField(const Params& p)
:   LLUICtrl(p),
    mSwatchWidth(p.swatch_width)
{
    LLLineEditor::Params ep;
    ep.name = "text";
    ep.rect = LLRect(mSwatchWidth + 3, getRect().getHeight(), getRect().getWidth(), 0);
    ep.follows.flags = FOLLOWS_ALL;
    ep.commit_on_focus_lost = true;
    mEditor = LLUICtrlFactory::create<LLLineEditor>(ep);
    mEditor->setCommitCallback([this](LLUICtrl*, const LLSD&) { onTextCommit(); });
    addChild(mEditor);
}

ALColorField::~ALColorField()
{
    closePopover();
}

void ALColorField::setValue(const LLSD& value)
{
    mText = value.asString();
    if (mEditor)
    {
        mEditor->setText(mText);
    }
}

LLSD ALColorField::getValue() const
{
    return mText;
}

bool ALColorField::resolved(LLColor4& color) const
{
    if (mText.empty())
    {
        return false;
    }
    if (LLUIColorTable::instance().colorExists(mText))
    {
        color = LLUIColorTable::instance().getColor(mText).get();
        return true;
    }
    return literalColor(mText, color);
}

void ALColorField::draw()
{
    static const LLUIColor edge = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
    const LLRect swatch(0, getRect().getHeight() - 2, mSwatchWidth, 2);

    LLColor4 color;
    if (resolved(color))
    {
        gl_rect_2d(swatch, color, true);
    }
    gl_rect_2d(swatch, edge.get(), false);
    LLUICtrl::draw();
}

bool ALColorField::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (x < mSwatchWidth)
    {
        openPopover();
        return true;
    }
    return LLUICtrl::handleMouseDown(x, y, mask);
}

void ALColorField::onFocusLost()
{
    LLUICtrl::onFocusLost();
}

void ALColorField::onTextCommit()
{
    mText = mEditor->getText();
    onCommit();
}

void ALColorField::chose(const std::string& name)
{
    setValue(name);
    closePopover();
    onCommit();
}

void ALColorField::openPopover()
{
    closePopover();

    LLFloater::Params p(LLFloater::getDefaultParams());
    p.can_close = false;
    p.can_minimize = false;
    p.can_resize = false;
    p.title = LLStringUtil::null;
    p.rect = LLRect(0, GRID_HEIGHT + 30, POPOVER_WIDTH, 0);

    ALColorPopover* popover = new ALColorPopover(p, [this](const std::string& name) { chose(name); });
    mPopover = popover->getHandle();

    // Under the field, and shoved back on screen if that would put it off.
    LLRect screen = calcScreenRect();
    LLRect where = popover->getRect();
    where.setLeftTopAndSize(screen.mLeft, screen.mBottom, where.getWidth(), where.getHeight());
    if (where.mBottom < 0)
    {
        where.translate(0, screen.getHeight() + where.getHeight());
    }
    popover->setRect(where);
    popover->openFloater();
    popover->setFocus(true);
}

void ALColorField::closePopover()
{
    if (LLFloater* popover = mPopover.get())
    {
        popover->closeFloater();
    }
    mPopover.markDead();
}
