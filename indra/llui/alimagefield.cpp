/**
 * @file alimagefield.cpp
 * @brief A field whose value is the name of a picture, shown as the picture
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

#include "alimagefield.h"

#include "alpopover.h"
#include "alspecimenlist.h"
#include "alstringmatch.h"

#include "llbutton.h"
#include "llfiltereditor.h"
#include "llfloater.h"
#include "lllineeditor.h"
#include "llrender2dutils.h"
#include "lltrans.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

static LLDefaultChildRegistry::Register<ALImageField> r("image_field");

namespace
{
    constexpr S32 ROW = 22;
    constexpr S32 MIN_WIDTH = 360;
    constexpr S32 MIN_HEIGHT = 300;

    // Where the popover opens, and where it opens next time.
    S32 sWidth = 520;
    S32 sHeight = 480;

    // The popover: a filter over a gallery of the pictures offered, each
    // as itself under its name, and a way through to the tool that edits
    // one. Choosing is choosing and not committing: the title says what
    // would be written, and it is written when the popover goes. Escape
    // abandons it.
    class ALImagePopover final : public ALPopover
    {
    public:
        AL_VIEW_TYPE(ALImagePopover, ALPopover);

        ALImagePopover(const LLFloater::Params& p, const std::string& value, std::vector<ALImageField::Choice> choices,
                       ALImageField::edit_t edit, const std::string& edit_label)
        :   ALPopover(p),
            mValue(value),
            mChoices(std::move(choices)),
            mEdit(std::move(edit))
        {
            const S32 width = getRect().getWidth();
            const S32 height = getRect().getHeight() - getHeaderHeight();
            const bool editable = mEdit != nullptr;
            const S32 button_width = editable ? 160 : 0;

            LLFilterEditor::Params fp;
            fp.name = "filter";
            fp.rect = LLRect(4, height - 4, width - 4 - button_width - (editable ? 6 : 0), height - 4 - ROW);
            fp.label = LLTrans::getString("ColorFieldFilter");
            fp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
            mFilter = LLUICtrlFactory::create<LLFilterEditor>(fp);
            mFilter->setCommitCallback([this](LLUICtrl* ctrl, const LLSD&) { filter(ctrl->getValue().asString()); });
            addChild(mFilter);

            if (editable)
            {
                LLButton::Params bp;
                bp.name = "edit";
                bp.label = edit_label;
                bp.rect = LLRect(width - 4 - button_width, height - 4, width - 4, height - 4 - ROW);
                bp.follows.flags = FOLLOWS_RIGHT | FOLLOWS_TOP;
                LLButton* edit_button = LLUICtrlFactory::create<LLButton>(bp);
                edit_button->setCommitCallback([this](LLUICtrl*, const LLSD&)
                {
                    // The name goes to the tool and the field alike: the
                    // tool opens on it, and the field keeps it. Copied
                    // out first, since settling is the end of this.
                    const ALImageField::edit_t edit = mEdit;
                    const std::string name = mValue;
                    settle();
                    edit(name);
                });
                addChild(edit_button);
            }

            ALSpecimenList::Params gp(LLUICtrlFactory::getDefaultParams<ALSpecimenList>());
            gp.name = "images";
            gp.rect = LLRect(2, height - 8 - ROW, width - 2, 2);
            gp.cell_width = 120;
            gp.cell_height = 72;
            gp.empty_headline = LLTrans::getString("ImageFieldNoMatch");
            gp.empty_sentence = LLTrans::getString("ImageFieldNoMatchHow");
            gp.follows.flags = FOLLOWS_ALL;
            mGallery = LLUICtrlFactory::create<ALSpecimenList>(gp);
            mGallery->onChose([this](const std::string& name)
            {
                mValue = name;
                setTitle(mValue);
            });
            addChild(mGallery);
            filter(LLStringUtil::null);
            setTitle(mValue);
        }

        const std::string& value() const { return mValue; }

        void onClose(bool app_quitting) override
        {
            sWidth = getRect().getWidth();
            sHeight = getRect().getHeight();
            ALPopover::onClose(app_quitting);
        }

    private:
        // The pictures are given once and the list narrows them itself.
        void filter(const std::string& text)
        {
            if (mGallery->count() == 0)
            {
                std::vector<ALSpecimenList::Specimen> specimens;
                for (const ALImageField::Choice& choice : mChoices)
                {
                    ALSpecimenList::Specimen specimen;
                    specimen.group = choice.heading;
                    specimen.label = choice.name;
                    specimen.value = choice.name;
                    specimen.image = choice.name;
                    specimen.toolTip = choice.name;
                    specimens.push_back(std::move(specimen));
                }
                mGallery->setSpecimens(std::move(specimens));
            }
            mGallery->filter(text);
            mGallery->setChosen(mValue);
        }

        std::string                         mValue;
        std::vector<ALImageField::Choice>   mChoices;
        ALImageField::edit_t                mEdit;
        LLFilterEditor*                     mFilter = nullptr;
        ALSpecimenList*                     mGallery = nullptr;
    };
}

ALImageField::Params::Params()
:   swatch_width("swatch_width", 22)
{
}

ALImageField::ALImageField(const Params& p)
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

ALImageField::~ALImageField()
{
    closePopover();
}

void ALImageField::setValue(const LLSD& value)
{
    setName(value.asString());
    if (mEditor)
    {
        mEditor->setText(mName);
    }
}

LLSD ALImageField::getValue() const
{
    return mName;
}

// The picture is looked up when the name changes, not on every frame it
// is drawn.
void ALImageField::setName(const std::string& name)
{
    mName = name;
    mImage = name.empty() ? LLPointer<LLUIImage>() : LLUI::getUIImage(name);
}

void ALImageField::setChoices(choices_t choices)
{
    mChoices = std::move(choices);
}

void ALImageField::setEditor(edit_t editor, std::string label)
{
    mEdit = std::move(editor);
    mEditLabel = std::move(label);
}

// The picture fitted into the swatch at its own proportions over a
// checkerboard, so an edge that is transparent reads as one.
void ALImageField::draw()
{
    static const LLUIColor edge = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
    const LLRect swatch(0, getRect().getHeight() - 2, mSwatchWidth, 2);

    gl_rect_2d_checkerboard(swatch, 1.f);
    if (mImage.notNull() && mImage->getWidth() > 0 && mImage->getHeight() > 0)
    {
        const F32 fit = llmin((F32)(swatch.getWidth() - 2) / (F32)mImage->getWidth(),
                              (F32)(swatch.getHeight() - 2) / (F32)mImage->getHeight());
        const S32 width = llmax(1, ll_round((F32)mImage->getWidth() * fit));
        const S32 height = llmax(1, ll_round((F32)mImage->getHeight() * fit));
        mImage->draw(swatch.mLeft + (swatch.getWidth() - width) / 2, swatch.mBottom + (swatch.getHeight() - height) / 2,
                     width, height);
    }
    gl_rect_2d(swatch, edge.get(), false);
    LLUICtrl::draw();
}

bool ALImageField::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (x < mSwatchWidth)
    {
        openPopover();
        return true;
    }
    return LLUICtrl::handleMouseDown(x, y, mask);
}

void ALImageField::onTextCommit()
{
    setName(mEditor->getText());
    onCommit();
}

// What the popover settled on, once it has gone.
void ALImageField::chose(const std::string& name)
{
    if (name != mName)
    {
        setValue(name);
        onCommit();
    }
}

void ALImageField::openPopover()
{
    closePopover();

    LLFloater::Params p(ALPopover::paramsFor(sWidth, sHeight, mName, /*resizable=*/true));
    p.min_width = MIN_WIDTH;
    p.min_height = MIN_HEIGHT;
    ALImagePopover* popover = new ALImagePopover(p, mName, mChoices ? mChoices() : std::vector<Choice>(), mEdit, mEditLabel);
    mPopover = popover->getDerivedHandle<ALPopover>();
    popover->onClosed([this, held = popover->getDerivedHandle<ALImagePopover>()](bool escaped)
    {
        if (ALImagePopover* said = held.get(); said && !escaped)
        {
            chose(said->value());
        }
        mPopover.markDead();
    });
    popover->openBeside(this);
}

void ALImageField::closePopover()
{
    if (ALPopover* popover = mPopover.get())
    {
        popover->escape();
    }
    mPopover.markDead();
}
