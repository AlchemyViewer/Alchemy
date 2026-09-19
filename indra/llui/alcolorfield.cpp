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
#include "alpopover.h"
#include "alstringmatch.h"

#include "llfiltereditor.h"
#include "llfloater.h"
#include "lllineeditor.h"
#include "llrender2dutils.h"
#include "llscrollcontainer.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "lluictrlfactory.h"
#include "lluicolortable.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALColorField> r("color_field");

namespace
{
    constexpr S32 SWATCH = 28;
    constexpr S32 GAP = 5;
    constexpr S32 HEADER = 26;          // the preview line over the tabs
    constexpr S32 ROW = 22;
    constexpr S32 MIN_WIDTH = 360;
    constexpr S32 MIN_HEIGHT = 340;
    // Where the popover opens the first time; it is resizable, and the
    // popover remembers where it was left after that.
    constexpr S32 WIDTH = 470;
    constexpr S32 HEIGHT = 460;

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

    // What the text comes to on screen, and whether it comes to anything: a
    // name colors.xml does not carry is drawn as nothing, which is the
    // lint's finding made visible.
    bool resolve(const std::string& text, LLColor4& color)
    {
        if (text.empty())
        {
            return false;
        }
        if (LLUIColorTable::instance().colorExists(text))
        {
            color = LLUIColorTable::instance().getColor(text).get();
            return true;
        }
        return literalColor(text, color);
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

        // The caller's own names first, as given; then every name the
        // table has, the skin's and the ones a person added, each as the
        // colour it comes to now rather than the one the skin shipped.
        ALColorSwatchGrid(const LLPanel::Params& p, chose_t chose, const std::vector<ALColorField::Choice>& first)
        :   LLPanel(p),
            mChose(std::move(chose))
        {
            for (const ALColorField::Choice& choice : first)
            {
                mAll.push_back({ choice.name, choice.color });
            }
            const size_t own = mAll.size();
            const LLUIColorTable& table = LLUIColorTable::instance();
            for (const auto& [name, color] : table.getLoadedColors())
            {
                mAll.push_back({ name, table.getColor(name).get() });
            }
            for (const auto& [name, color] : table.getUserColors())
            {
                if (!table.getLoadedColors().contains(name))
                {
                    mAll.push_back({ name, color.get() });
                }
            }
            std::sort(mAll.begin() + own, mAll.end(),
                      [](const Entry& a, const Entry& b) { return a.name < b.name; });
            filter(LLStringUtil::null);
        }

        void setChosen(const std::string& name) { mChosen = name; }

        void filter(const std::string& text)
        {
            mShown.clear();
            for (const Entry& entry : mAll)
            {
                if (ALStringMatch::containsNoCase(entry.name, text))
                {
                    mShown.push_back(&entry);
                }
            }
            reflow();
        }

        // As many across as the width holds, so widening the popover shows
        // more of them rather than the same twelve further apart.
        S32 columns() const
        {
            return llmax(1, (getRect().getWidth() - GAP) / (SWATCH + GAP));
        }

        void reflow()
        {
            const S32 rows = ((S32)mShown.size() + columns() - 1) / columns();
            reshape(getRect().getWidth(), llmax(1, rows) * (SWATCH + GAP) + GAP, false);
            mHover = -1;
        }

        void reshape(S32 width, S32 height, bool called_from_parent = true) override
        {
            const bool wider = width != getRect().getWidth();
            LLPanel::reshape(width, height, called_from_parent);
            if (wider)
            {
                reflow();
            }
        }

        void draw() override
        {
            LLPanel::draw();
            static const LLUIColor edge = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
            for (size_t i = 0; i < mShown.size(); ++i)
            {
                const LLRect cell = rectOf((S32)i);
                gl_rect_2d(cell, mShown[i]->color, true);
                // What is chosen keeps a ring around it, so that a popover
                // left open still says what it is about to give back.
                if (mShown[i]->name == mChosen)
                {
                    LLRect ring(cell);
                    ring.stretch(2);
                    gl_rect_2d(ring, LLColor4::white, false);
                    gl_rect_2d(cell, LLColor4::black, false);
                }
                else
                {
                    gl_rect_2d(cell, (S32)i == mHover ? LLColor4::white : edge.get(), false);
                }
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
                mChosen = mShown[which]->name;
                mChose(mChosen);
                return true;
            }
            return LLPanel::handleMouseDown(x, y, mask);
        }

    private:
        LLRect rectOf(S32 index) const
        {
            const S32 column = index % columns();
            const S32 row = index / columns();
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
        std::string                 mChosen;
        chose_t                     mChose;
        S32                         mHover = -1;
    };

    // The popover. A colour is arrived at rather than known, so choosing
    // here is choosing and not committing: the swatch at the top says what
    // would be written, and it is written when the popover goes. Escape
    // abandons it. It resizes, and the size it is left at is the size it
    // opens at next time, because how much of a colour wheel someone wants
    // to see is not something this can decide for them.
    class ALColorPopover final : public ALPopover
    {
    public:
        AL_VIEW_TYPE(ALColorPopover, ALPopover);

        ALColorPopover(const LLFloater::Params& p, const std::string& value, ALColorField::resolver_t resolver,
                       const std::vector<ALColorField::Choice>& choices)
        :   ALPopover(p),
            mValue(value),
            mResolver(std::move(resolver))
        {
            const S32 width = getRect().getWidth();
            // A floater draws its own title across the top of its rect, and
            // what is laid out from the top of the rect is laid out under
            // it. The preview line starts where the title ends.
            const S32 height = getRect().getHeight() - getHeaderHeight();

            LLFilterEditor::Params fp;
            fp.name = "filter";
            fp.rect = LLRect(HEADER + 8, height - 4, width - 4, height - 4 - ROW);
            fp.label = LLTrans::getString("ColorFieldFilter");
            fp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
            mFilter = LLUICtrlFactory::create<LLFilterEditor>(fp);
            addChild(mFilter);

            LLTabContainer::Params tp;
            tp.name = "tabs";
            tp.rect = LLRect(2, height - 8 - ROW, width - 2, 2);
            tp.tab_position = LLTabContainer::TOP;
            tp.tab_height = 20;
            tp.follows.flags = FOLLOWS_ALL;
            LLTabContainer* tabs = LLUICtrlFactory::create<LLTabContainer>(tp);
            addChild(tabs);

            const LLRect body(0, tabs->getRect().getHeight() - 22, tabs->getRect().getWidth() - 6, 0);

            LLPanel::Params np;
            np.name = "names";
            np.label = LLTrans::getString("ColorFieldNames");
            np.rect = body;
            LLPanel* names = LLUICtrlFactory::create<LLPanel>(np);

            LLScrollContainer::Params sp;
            sp.name = "scroll";
            sp.rect = body;
            sp.follows.flags = FOLLOWS_ALL;
            LLScrollContainer* scroll = LLUICtrlFactory::create<LLScrollContainer>(sp);
            names->addChild(scroll);

            LLPanel::Params gp;
            gp.name = "swatches";
            gp.rect = LLRect(0, body.getHeight(), scroll->getContentWindowRect().getWidth(), 0);
            gp.background_visible = false;
            gp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
            mGrid = new ALColorSwatchGrid(gp, [this](const std::string& name)
            {
                chose(name);
                showInPicker(name);
            }, choices);
            mGrid->setChosen(mValue);
            scroll->addChild(mGrid);
            // A tab container picks its first tab when it is built, and this
            // one was built with none; a tab added afterwards is added
            // hidden, so the one to open on has to be asked for.
            tabs->addTabPanel(LLTabContainer::TabPanelParams().panel(names).select_tab(true));

            // The other way a file writes a colour, for the times the
            // table has no name for what is wanted.
            LLPanel::Params cp;
            cp.name = "custom";
            cp.label = LLTrans::getString("ColorFieldCustom");
            cp.rect = body;
            LLPanel* custom = LLUICtrlFactory::create<LLPanel>(cp);

            ALColorPicker::Params pp;
            pp.name = "picker";
            pp.rect = LLRect(4, body.getHeight() - 4, body.getWidth() - 4, 4);
            pp.follows.flags = FOLLOWS_ALL;
            mPicker = LLUICtrlFactory::create<ALColorPicker>(pp);
            showInPicker(mValue);
            mPicker->setCommitCallback([this](LLUICtrl* ctrl, const LLSD&)
            {
                chose(ctrl->getValue().asString());
            });
            custom->addChild(mPicker);
            tabs->addTabPanel(custom);

            mFilter->setCommitCallback([this](LLUICtrl* ctrl, const LLSD&)
            {
                mGrid->filter(ctrl->getValue().asString());
            });
            refreshPreview();
        }

        // What was chosen, as the file writes it.
        const std::string& value() const { return mValue; }

        void draw() override
        {
            ALPopover::draw();
            // What would be written, drawn where it can be compared with
            // what is under the pointer.
            static const LLUIColor edge = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
            const S32 top = getRect().getHeight() - getHeaderHeight();
            const LLRect swatch(4, top - 4, 4 + HEADER - 8, top - 4 - (ROW - 2));
            LLColor4 color;
            if (comesTo(mValue, color))
            {
                gl_rect_2d(swatch, color, true);
            }
            gl_rect_2d(swatch, edge.get(), false);
        }

    private:
        void chose(const std::string& value)
        {
            mValue = value;
            mGrid->setChosen(value);
            refreshPreview();
        }

        void refreshPreview()
        {
            setTitle(mValue);
        }

        // The picker starts from what the field has, and follows a name
        // chosen from the grid: a name is what a file writes, and the
        // picker reads numbers, so the name is looked up for it.
        void showInPicker(const std::string& value)
        {
            LLColor4 color;
            if (comesTo(value, color))
            {
                mPicker->setColor(color);
            }
        }

        // What a text comes to, asked of the field's own resolver first,
        // the way the field asks.
        bool comesTo(const std::string& value, LLColor4& color) const
        {
            return (mResolver && mResolver(value, color)) || resolve(value, color);
        }

        std::string         mValue;
        ALColorField::resolver_t mResolver;
        LLFilterEditor*     mFilter = nullptr;
        ALColorSwatchGrid*  mGrid = nullptr;
        ALColorPicker*      mPicker = nullptr;
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
    setText(value.asString());
    if (mEditor)
    {
        mEditor->setText(mText);
    }
}

// What the text comes to is worked out when the text changes, not on
// every frame the swatch is drawn: a name is a table lookup and four
// numbers are a parse, and neither is what a draw should be doing.
void ALColorField::setText(const std::string& text)
{
    mText = text;
    resolveText();
}

void ALColorField::resolveText() const
{
    mHasColor = (mResolver && mResolver(mText, mColor)) || resolve(mText, mColor);
    mSeenGeneration = LLUIColorTable::instance().generation();
}

LLSD ALColorField::getValue() const
{
    return mText;
}

bool ALColorField::textToColor(const std::string& text, LLColor4& color)
{
    return resolve(text, color);
}

void ALColorField::setResolver(resolver_t resolver)
{
    mResolver = std::move(resolver);
    setText(mText);
}

void ALColorField::setChoices(choices_t choices)
{
    mChoices = std::move(choices);
}

// A name means what the table says now: a colour changed elsewhere
// shows here without the text having changed.
bool ALColorField::resolved(LLColor4& color) const
{
    if (mSeenGeneration != LLUIColorTable::instance().generation())
    {
        resolveText();
    }
    color = mColor;
    return mHasColor;
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

void ALColorField::onTextCommit()
{
    setText(mEditor->getText());
    onCommit();
}

// What the popover settled on, once it has gone.
void ALColorField::chose(const std::string& name)
{
    setValue(name);
    onCommit();
}

void ALColorField::openPopover()
{
    closePopover();

    LLFloater::Params p(ALPopover::paramsRemembered("color_field", WIDTH, HEIGHT, mText));
    p.min_width = MIN_WIDTH;
    p.min_height = MIN_HEIGHT;
    ALColorPopover* popover = new ALColorPopover(p, mText, mResolver, mChoices ? mChoices() : std::vector<Choice>());
    mPopover = popover->getDerivedHandle<ALPopover>();
    // Told as it goes, while it still holds what was chosen; escaped is
    // the one way out that keeps what the field had.
    popover->onClosed([this, held = popover->getDerivedHandle<ALColorPopover>()](bool escaped)
    {
        if (ALColorPopover* said = held.get(); said && !escaped)
        {
            chose(said->value());
        }
        mPopover.markDead();
    });
    popover->openBeside(this);
}

// Closed from this side rather than from its own: whatever was chosen is
// dropped, because this is either about to open another one or about to be
// deleted, and a popover closing itself is the path that keeps a choice.
void ALColorField::closePopover()
{
    if (ALPopover* popover = mPopover.get())
    {
        popover->escape();
    }
    mPopover.markDead();
}
