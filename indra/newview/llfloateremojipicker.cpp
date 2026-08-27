/**
 * @file llfloateremojipicker.cpp
 *
 * $LicenseInfo:firstyear=2003&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfloateremojipicker.h"

#include "llappviewer.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llemojidictionary.h"
#include "llemojihelper.h"
#include "llfloaterreg.h"
#include "llkeyboard.h"
#include "llrender2dutils.h"
#include "llscrollcontainer.h"
#include "llscrollingpanellist.h"
#include "llscrolllistctrl.h"
#include "llscrolllistitem.h"
#include "llsdserialize.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "lluictrlfactory.h"
#include "llviewerchat.h"
#include "llviewercontrol.h"

namespace {
// The following variables and constants are used for storing the floater state
// between different lifecycles of the floater and different sissions of the viewer

// Floater constants
static const S32 ALL_EMOJIS_GROUP_INDEX = -2;
// https://www.compart.com/en/unicode/U+1F50D
static const S32 ALL_EMOJIS_IMAGE_INDEX = 0x1F50D;
static const S32 USED_EMOJIS_GROUP_INDEX = -1;
// https://www.compart.com/en/unicode/U+23F2
static const S32 USED_EMOJIS_IMAGE_INDEX = 0x23F2;
// https://www.compart.com/en/unicode/U+1F6D1
static const S32 EMPTY_LIST_IMAGE_INDEX = 0x1F6D1;
// The following categories should follow the required alphabetic order
static const std::string FREQUENTLY_USED_CATEGORY = "frequently used";

// Floater state related variables
// Per-user most-recently-used and most-frequently-used lists. Stored as
// LLWString so multi-codepoint emoji (ZWJ families, flag pairs, keycap, tag
// subdivision flags) count as a single logical emoji.
static std::list<LLWString> sRecentlyUsed;
static std::list<std::pair<LLWString, U32>> sFrequentlyUsed;

// State file related values
static std::string sStateFileName;
static const std::string sKeyRecentlyUsed("RecentlyUsed");
static const std::string sKeyFrequentlyUsed("FrequentlyUsed");
}

class LLEmojiGridRow : public LLScrollingPanel
{
public:
    LLEmojiGridRow(const LLPanel::Params& panel_params,
        const LLScrollingPanelList::Params& list_params)
        : LLScrollingPanel(panel_params)
        , mList(new LLScrollingPanelList(list_params))
    {
        addChild(mList);
    }

    virtual void updatePanel(bool allow_modify) override {}

public:
    LLScrollingPanelList* mList;
};

class LLEmojiGridDivider : public LLScrollingPanel
{
public:
    LLEmojiGridDivider(const LLPanel::Params& panel_params, std::string text)
        : LLScrollingPanel(panel_params)
        , mText(utf8string_to_wstring(text))
    {
    }

    virtual void draw() override
    {
        LLScrollingPanel::draw();

        F32 x = 4; // padding-left
        F32 y = (F32)(getRect().getHeight() / 2);
        LLFontGL::getFontSansSerif()->render(
            mText,                           // wstr
            0,                               // begin_offset
            x,                               // x
            y,                               // y
            LLColor4::white,                 // color
            LLFontGL::LEFT,                  // halign
            LLFontGL::VCENTER,               // valign
            LLFontGL::NORMAL,                // style
            LLFontGL::DROP_SHADOW_SOFT,      // shadow
            static_cast<S32>(mText.size())); // max_chars
    }

    virtual void updatePanel(bool allow_modify) override {}

private:
    const LLWString mText;
};

class LLEmojiGridIcon : public LLScrollingPanel
{
public:
    typedef std::function<void(LLEmojiGridIcon*)> right_click_cb_t;

    LLEmojiGridIcon(
        const LLPanel::Params& panel_params
        , const LLEmojiSearchResult& emoji)
        : LLScrollingPanel(panel_params)
        , mData(emoji)
        , mChar(emoji.Character)
        , mHasVariants(false)
    {
    }

    virtual void draw() override
    {
        LLScrollingPanel::draw();

        F32 x = (F32)(getRect().getWidth() / 2);
        F32 y = (F32)(getRect().getHeight() / 2);
        LLFontGL::getFontEmojiLarge()->render(
            mChar,                      // wstr
            0,                          // begin_offset
            x,                          // x
            y,                          // y
            LLColor4::white,            // color
            LLFontGL::HCENTER,          // halign
            LLFontGL::VCENTER,          // valign
            LLFontGL::NORMAL,           // style
            LLFontGL::DROP_SHADOW_SOFT, // shadow
            static_cast<S32>(mChar.size()), // max_chars — full cluster
            S32_MAX,
            nullptr,
            false,
            true);

        // Affordance: a tiny dot in the top-right corner indicates this
        // emoji has alternates. Right-click (or long-press) opens them.
        if (mHasVariants)
        {
            S32 w = getRect().getWidth();
            S32 h = getRect().getHeight();
            gl_rect_2d(w - 5, h - 2, w - 2, h - 5,
                       LLColor4(1.f, 1.f, 1.f, 0.6f),
                       /*filled=*/true);
        }
    }

    virtual bool handleRightMouseDown(S32 x, S32 y, MASK mask) override
    {
        if (mRightClickCb)
        {
            mRightClickCb(this);
            return true;
        }
        return LLScrollingPanel::handleRightMouseDown(x, y, mask);
    }

    virtual void updatePanel(bool allow_modify) override {}

    const LLEmojiSearchResult& getData() const { return mData; }
    const LLWString& getChar() const { return mChar; }
    void setHasVariants(bool b) { mHasVariants = b; }
    void setRightClickCallback(right_click_cb_t cb) { mRightClickCb = std::move(cb); }

private:
    const LLEmojiSearchResult mData;
    const LLWString mChar;
    bool mHasVariants;
    right_click_cb_t mRightClickCb;
};

class LLEmojiPreviewPanel : public LLPanel
{
public:
    LLEmojiPreviewPanel()
        : LLPanel()
    {
    }

    void setIcon(const LLEmojiGridIcon* icon)
    {
        if (icon)
        {
            setData(icon->getData().Character, icon->getData().String, icon->getData().Begin, icon->getData().End);
        }
        else
        {
            setData(LLWString(), LLStringUtil::null, 0, 0);
        }
    }

    void setData(const LLWString& emoji, std::string title, size_t begin, size_t end)
    {
        mWStr = emoji;
        mTitle = utf8str_to_wstring(title);
        mBegin = begin;
        mEnd = end;
    }

    virtual void draw() override
    {
        LLPanel::draw();

        S32 clientHeight = getRect().getHeight();
        S32 clientWidth = getRect().getWidth();
        S32 iconWidth = clientHeight;

        F32 centerX = 0.5f * iconWidth;
        F32 centerY = 0.5f * clientHeight;
        drawIcon(centerX, centerY - 1, iconWidth);

        static LLUIColor textColor = LLUIColorTable::instance().getColor("MenuItemEnabledColor", LLColor4(0.75f, 0.75f, 0.75f, 1.0f));
        S32 max_pixels = clientWidth - iconWidth;
        drawName((F32)iconWidth, centerY, max_pixels, textColor.get());
    }

protected:
    void drawIcon(F32 x, F32 y, S32 max_pixels)
    {
        LLFontGL::getFontEmojiHuge()->render(
            mWStr,                      // wstr
            0,                          // begin_offset
            x,                          // x
            y,                          // y
            LLColor4::white,            // color
            LLFontGL::HCENTER,          // halign
            LLFontGL::VCENTER,          // valign
            LLFontGL::NORMAL,           // style
            LLFontGL::DROP_SHADOW_SOFT, // shadow
            static_cast<S32>(mWStr.size()), // max_chars — full cluster
            max_pixels,// max_pixels
            nullptr,
            false,
            true);
    }

    void drawName(F32 x, F32 y, S32 max_pixels, const LLColor4& color)
    {
        F32 x0 = x;
        F32 x1 = (F32)max_pixels;
        LLFontGL* font = LLFontGL::getFontSansSerifHuge();
        if (mBegin)
        {
            LLWString text = mTitle.substr(0, mBegin);
            font->render(
                text,                          // text
                0,                             // begin_offset
                x0,                            // x
                y,                             // y
                color,                         // color
                LLFontGL::LEFT,                // halign
                LLFontGL::VCENTER,             // valign
                LLFontGL::NORMAL,              // style
                LLFontGL::DROP_SHADOW_SOFT,    // shadow
                static_cast<S32>(text.size()), // max_chars
                (S32)x1);                      // max_pixels
            F32 dx = font->getWidthF32(text);
            x0 += dx;
            x1 -= dx;
        }
        if (x1 > 0 && mEnd > mBegin)
        {
            LLWString text = mTitle.substr(mBegin, mEnd - mBegin);
            font->render(
                text,                          // text
                0,                             // begin_offset
                x0,                            // x
                y,                             // y
                LLColor4::yellow6,             // color
                LLFontGL::LEFT,                // halign
                LLFontGL::VCENTER,             // valign
                LLFontGL::NORMAL,              // style
                LLFontGL::DROP_SHADOW_SOFT,    // shadow
                static_cast<S32>(text.size()), // max_chars
                (S32)x1);                      // max_pixels
            F32 dx = font->getWidthF32(text);
            x0 += dx;
            x1 -= dx;
        }
        if (x1 > 0 && mEnd < mTitle.size())
        {
            LLWString text = mEnd ? mTitle.substr(mEnd) : mTitle;
            font->render(
                text,                          // text
                0,                             // begin_offset
                x0,                            // x
                y,                             // y
                color,                         // color
                LLFontGL::LEFT,                // halign
                LLFontGL::VCENTER,             // valign
                LLFontGL::NORMAL,              // style
                LLFontGL::DROP_SHADOW_SOFT,    // shadow
                static_cast<S32>(text.size()), // max_chars
                (S32)x1);                      // max_pixels
        }
    }

private:
    LLWString mWStr;
    LLWString mTitle;
    size_t mBegin;
    size_t mEnd;
};

LLFloaterEmojiPicker::LLFloaterEmojiPicker(const LLSD& key)
: super(key)
{
    // This floater should hover on top of our dependent (with the dependent having the focus)
    setFocusStealsFrontmost(false);
    setBackgroundVisible(false);
    setAutoFocus(false);

    loadState();
}

bool LLFloaterEmojiPicker::postBuild()
{
    mGroups = getChild<LLPanel>("Groups");
    mBadge = getChild<LLPanel>("Badge");
    mToneStrip = getChild<LLPanel>("ToneStrip");
    mEmojiScroll = getChild<LLScrollContainer>("EmojiGridContainer");
    mEmojiGrid = getChild<LLScrollingPanelList>("EmojiGrid");
    mDummy = getChild<LLTextBox>("Dummy");

    mPreview = new LLEmojiPreviewPanel();
    mPreview->setVisible(false);
    addChild(mPreview);

    buildToneStrip();

    return LLFloater::postBuild();
}

void LLFloaterEmojiPicker::onOpen(const LLSD& key)
{
    mHint = key["hint"].asString();

    LLEmojiHelper::instance().setIsHideDisabled(mHint.empty());
    mFilterPattern = mHint;

    initialize();

    gFloaterView->adjustToFitScreen(this, false);
}

void LLFloaterEmojiPicker::onClose(bool app_quitting)
{
    // Hide the flyout without deleting — onClose can run inside the same
    // callstack as a flyout cell's mouse-up (commitVariant → hideFloater →
    // helper teardown → onClose), and deleting that cell mid-dispatch is
    // UB. The next showVariantFlyout call clears the stale panel before
    // building a new one.
    if (mVariantFlyout)
    {
        mVariantFlyout->setVisible(false);
    }

    if (!app_quitting)
    {
        LLEmojiHelper::instance().hideHelper(nullptr, true);
    }
}

void LLFloaterEmojiPicker::dirtyRect()
{
    super::dirtyRect();

    if (!mPreview)
        return;

    const S32 HPADDING = 4;
    const S32 VOFFSET = 12;
    LLRect rect(HPADDING, mDummy->getRect().mTop + 6, getRect().getWidth() - HPADDING, VOFFSET);
    if (mPreview->getRect() != rect)
    {
        mPreview->setRect(rect);
    }

    if (mEmojiScroll && mEmojiGrid)
    {
        S32 outer_width = mEmojiScroll->getRect().getWidth();
        S32 inner_width = mEmojiGrid->getRect().getWidth();
        if (outer_width != inner_width)
        {
            resizeGroupButtons();
            fillEmojis(true);
        }
    }

    if (mToneStrip && mToneStrip->getRect().getWidth() != mToneStripLastWidth)
    {
        layoutToneStripButtons();
    }
}

void LLFloaterEmojiPicker::initialize()
{
    S32 groupIndex = mSelectedGroupIndex && mSelectedGroupIndex <= mFilteredEmojiGroups.size() ?
        mFilteredEmojiGroups[mSelectedGroupIndex - 1] : ALL_EMOJIS_GROUP_INDEX;

    fillGroups();

    if (mFilteredEmojis.empty())
    {
        if (!mHint.empty())
        {
            // Hack: Trying to open floater, search for a match,
            // and hide floater immediately if no match found,
            // instead of checking prior to opening
            hideFloater();
            return;
        }

        if (mVariantFlyout)
        {
            dismissVariantFlyout();
        }

        mGroups->setVisible(false);
        mFocusedIconRow = -1;
        mFocusedIconCol = -1;
        mFocusedIcon = nullptr;
        mHoveredIcon = nullptr;
        mFlyoutBaseIcon = nullptr;
        mEmojiScroll->goToTop();
        mEmojiGrid->clearPanels();

        if (mFilterPattern.empty())
        {
            showPreview(false);
        }
        else
        {
            std::size_t begin, end;
            LLStringUtil::format_map_t args;
            args["[FILTER]"] = mFilterPattern.substr(1);
            std::string title(getString("text_no_emoji_for_filter", args));
            LLEmojiDictionary::searchInShortCode(begin, end, title, mFilterPattern);
            mPreview->setData(LLWString(1, (llwchar)EMPTY_LIST_IMAGE_INDEX), title, begin, end);
            showPreview(true);
        }
        return;
    }

    if (!mHint.empty() && getSoundFlags() == LLView::SILENT)
    {
        // Sounds were supressed
        make_ui_sound("UISndWindowOpen");
    }

    mGroups->setVisible(true);
    mPreview->setIcon(nullptr);
    showPreview(true);

    mSelectedGroupIndex = groupIndex == ALL_EMOJIS_GROUP_INDEX ? 0 :
        static_cast<U32>((1 + std::distance(mFilteredEmojiGroups.begin(),
            std::find(mFilteredEmojiGroups.begin(), mFilteredEmojiGroups.end(), groupIndex))) %
        (1 + mFilteredEmojiGroups.size()));

    mGroupButtons[mSelectedGroupIndex]->setToggleState(true);
    mGroupButtons[mSelectedGroupIndex]->setUseFontColor(true);

    fillEmojis();
}

void LLFloaterEmojiPicker::fillGroups()
{
    // Do not use deleteAllChildren() because mBadge shouldn't be removed
    for (LLButton* button : mGroupButtons)
    {
        mGroups->removeChild(button);
        button->die();
    }
    mFilteredEmojiGroups.clear();
    mFilteredEmojis.clear();
    mGroupButtons.clear();

    LLButton::Params params;
    params.font = LLFontGL::getFontEmojiLarge();

    LLRect rect;
    rect.mTop = mGroups->getRect().getHeight();
    rect.mBottom = mBadge->getRect().getHeight();

    // Create button for "All categories"
    params.name = "all_categories";
    createGroupButton(params, rect, LLWString(1, (llwchar)ALL_EMOJIS_IMAGE_INDEX));

    // Create group and button for "Frequently used"
    if (!sFrequentlyUsed.empty())
    {
        std::map<std::string, std::vector<LLEmojiSearchResult>> cats;
        fillCategoryFrequentlyUsed(cats);

        if (!cats.empty())
        {
            mFilteredEmojiGroups.push_back(USED_EMOJIS_GROUP_INDEX);
            mFilteredEmojis.emplace_back(cats);
            params.name = "used_categories";
            createGroupButton(params, rect, LLWString(1, (llwchar)USED_EMOJIS_IMAGE_INDEX));
        }
    }

    const std::vector<LLEmojiGroup>& groups = LLEmojiDictionary::instance().getGroups();

    // List all categories in the dictionary
    for (U32 i = 0; i < groups.size(); ++i)
    {
        std::map<std::string, std::vector<LLEmojiSearchResult>> cats;

        fillGroupEmojis(cats, i);

        if (!cats.empty())
        {
            mFilteredEmojiGroups.push_back(i);
            mFilteredEmojis.emplace_back(cats);
            params.name = "group_" + std::to_string(i);
            createGroupButton(params, rect, groups[i].Character);
        }
    }

    resizeGroupButtons();
}

void LLFloaterEmojiPicker::fillCategoryFrequentlyUsed(std::map<std::string, std::vector<LLEmojiSearchResult>>& cats)
{
    if (sFrequentlyUsed.empty())
        return;

    std::vector<LLEmojiSearchResult> emojis;

    // In case of empty mFilterPattern we'd use sFrequentlyUsed directly
    if (!mFilterPattern.empty())
    {
        // List all emojis in "Frequently used"
        const LLEmojiDictionary& dict = LLEmojiDictionary::instance();
        const LLEmojiDictionary::emoji2descr_map_t& emoji2descr = dict.getEmoji2Descr();
        std::size_t begin, end;
        for (const auto& emoji : sFrequentlyUsed)
        {
            // Recents may carry a variant sequence (e.g. 👍🏿) that has no
            // top-level descriptor; resolve through the variant map to its
            // base so we can still show + filter it.
            auto e2d = emoji2descr.find(emoji.first);
            const LLEmojiDescriptor* descr = (e2d != emoji2descr.end()) ? e2d->second : dict.getBaseFromVariant(emoji.first);
            if (descr && !descr->ShortCodes.empty())
            {
                for (const std::string& shortcode : descr->ShortCodes)
                {
                if (LLEmojiDictionary::searchInShortCode(begin, end, shortcode, mFilterPattern))
                {
                    emojis.emplace_back(emoji.first, shortcode, begin, end);
                }
            }
        }
        }
        if (emojis.empty())
            return;
    }

    cats.emplace(std::make_pair(FREQUENTLY_USED_CATEGORY, emojis));
}

void LLFloaterEmojiPicker::fillGroupEmojis(std::map<std::string, std::vector<LLEmojiSearchResult>>& cats, U32 index)
{
    const std::vector<LLEmojiGroup>& groups = LLEmojiDictionary::instance().getGroups();
    const LLEmojiDictionary::cat2descrs_map_t& category2Descr = LLEmojiDictionary::instance().getCategory2Descrs();

    for (const std::string& category : groups[index].Categories)
    {
        const LLEmojiDictionary::cat2descrs_map_t::const_iterator& c2d = category2Descr.find(category);
        if (c2d == category2Descr.end())
            continue;

        std::vector<LLEmojiSearchResult> emojis;

        // In case of empty mFilterPattern we'd use category2Descr directly
        if (!mFilterPattern.empty())
        {
            // List all emojis in category
            std::size_t begin, end;
            for (const LLEmojiDescriptor* descr : c2d->second)
            {
                if (!descr->ShortCodes.empty())
                {
                    for (const std::string& shortcode : descr->ShortCodes)
                    {
                    if (LLEmojiDictionary::searchInShortCode(begin, end, shortcode, mFilterPattern))
                    {
                        emojis.emplace_back(descr->Character, shortcode, begin, end);
                    }
                }
            }
            }
            if (emojis.empty())
                continue;
        }

        cats.emplace(std::make_pair(category, emojis));
    }
}

void LLFloaterEmojiPicker::createGroupButton(LLButton::Params& params, const LLRect& rect, const LLWString& emoji)
{
    LLButton* button = LLUICtrlFactory::create<LLButton>(params);
    button->setClickedCallback([this](LLUICtrl* ctrl, const LLSD&) { onGroupButtonClick(ctrl); });
    button->setMouseEnterCallback([this](LLUICtrl* ctrl, const LLSD&) { onGroupButtonMouseEnter(ctrl); });
    button->setMouseLeaveCallback([this](LLUICtrl* ctrl, const LLSD&) { onGroupButtonMouseLeave(ctrl); });

    button->setRect(rect);
    button->setTabStop(false);
    button->setLabel(LLUIString(emoji));
    button->setUseFontColor(false);

    mGroupButtons.push_back(button);
    mGroups->addChild(button);
}

void LLFloaterEmojiPicker::resizeGroupButtons()
{
    U32 groupCount = (U32)mGroupButtons.size();
    if (!groupCount)
        return;

    S32 totalWidth = mGroups->getRect().getWidth();
    S32 badgeWidth = totalWidth / groupCount;
    S32 leftOffset = (totalWidth - badgeWidth * groupCount) / 2;

    for (U32 i = 0; i < groupCount; ++i)
    {
        LLRect rect = mGroupButtons[i]->getRect();
        rect.mLeft = leftOffset + badgeWidth * i;
        rect.mRight = rect.mLeft + badgeWidth;
        mGroupButtons[i]->setRect(rect);
    }

    LLRect rect = mBadge->getRect();
    rect.mLeft = leftOffset + badgeWidth * mSelectedGroupIndex;
    rect.mRight = rect.mLeft + badgeWidth;
    mBadge->setRect(rect);
}

void LLFloaterEmojiPicker::selectEmojiGroup(U32 index)
{
    if (index == mSelectedGroupIndex || index >= mGroupButtons.size())
        return;

    if (mSelectedGroupIndex < mGroupButtons.size())
    {
        mGroupButtons[mSelectedGroupIndex]->setUseFontColor(false);
        mGroupButtons[mSelectedGroupIndex]->setToggleState(false);
    }

    mSelectedGroupIndex = index;
    mGroupButtons[mSelectedGroupIndex]->setToggleState(true);
    mGroupButtons[mSelectedGroupIndex]->setUseFontColor(true);

    LLButton* button = mGroupButtons[mSelectedGroupIndex];
    LLRect rect = mBadge->getRect();
    rect.mLeft = button->getRect().mLeft;
    rect.mRight = button->getRect().mRight;
    mBadge->setRect(rect);

    fillEmojis();
}

void LLFloaterEmojiPicker::fillEmojis(bool fromResize)
{
    S32 scrollbar_size = mEmojiScroll->getSize();
    if (scrollbar_size < 0)
    {
        static LLUICachedControl<S32> scrollbar_size_control("UIScrollbarSize", 0);
        scrollbar_size = scrollbar_size_control;
    }

    const S32 scroll_width = mEmojiScroll->getRect().getWidth();
    const S32 client_width = scroll_width - scrollbar_size - mEmojiScroll->getBorderWidth() * 2;
    const S32 grid_padding = mEmojiGrid->getPadding();
    const S32 icon_spacing = mEmojiGrid->getSpacing();
    const S32 row_width = client_width - grid_padding * 2;
    const S32 icon_size = 28; // icon width and height
    const S32 max_icons = llmax(1, (row_width + icon_spacing) / (icon_size + icon_spacing));

    // Optimization: don't rearrange for different widths with the same maxIcons
    if (fromResize && (max_icons == mRecentMaxIcons))
        return;

    mRecentMaxIcons = max_icons;

    // Tear down the flyout first — its base-icon pointer is about to
    // dangle once we clear the grid panels.
    if (mVariantFlyout)
    {
        dismissVariantFlyout();
    }

    mFocusedIconRow = 0;
    mFocusedIconCol = 0;
    mFocusedIcon = nullptr;
    mHoveredIcon = nullptr;
    mFlyoutBaseIcon = nullptr;
    mEmojiScroll->goToTop();
    mEmojiGrid->clearPanels();
    mPreview->setIcon(nullptr);

    if (mEmojiGrid->getRect().getWidth() != client_width)
    {
        LLRect rect = mEmojiGrid->getRect();
        rect.mRight = rect.mLeft + client_width;
        mEmojiGrid->setRect(rect);
    }

    LLPanel::Params row_panel_params;
    row_panel_params.rect = LLRect(0, icon_size, row_width, 0);

    LLScrollingPanelList::Params row_list_params;
    row_list_params.rect = row_panel_params.rect;
    row_list_params.is_horizontal = true;
    row_list_params.padding = 0;
    row_list_params.spacing = icon_spacing;

    LLPanel::Params icon_params;
    LLRect icon_rect(0, icon_size, icon_size, 0);

    static LLUIColor bg_color = LLUIColorTable::instance().getColor("MenuItemHighlightBgColor", LLColor4(0.75f, 0.75f, 0.75f, 1.0f));

    if (!mSelectedGroupIndex)
    {
        // List all groups
        for (const auto& group : mFilteredEmojis)
        {
            // List all categories in the group
            for (const auto& category : group)
            {
                // List all emojis in the category
                fillEmojisCategory(category.second, category.first, row_panel_params,
                    row_list_params, icon_params, icon_rect, max_icons, bg_color);
            }
        }
    }
    else
    {
        // List all categories in the selected group
        const auto& group = mFilteredEmojis[mSelectedGroupIndex - 1];
        for (const auto& category : group)
        {
            // List all emojis in the category
            fillEmojisCategory(category.second, category.first, row_panel_params,
                row_list_params, icon_params, icon_rect, max_icons, bg_color);
        }
    }

    if (mEmojiGrid->getPanelList().empty())
    {
        showPreview(false);
        mFocusedIconRow = -1;
        mFocusedIconCol = -1;
        if (!mHint.empty())
        {
            hideFloater();
        }
    }
    else
    {
        showPreview(true);
        mFocusedIconRow = 0;
        mFocusedIconCol = 0;
        moveFocusedIconNext();
    }
}

void LLFloaterEmojiPicker::fillEmojisCategory(const std::vector<LLEmojiSearchResult>& emojis,
    const std::string& category, const LLPanel::Params& row_panel_params, const LLUICtrl::Params& row_list_params,
    const LLPanel::Params& icon_params, const LLRect& icon_rect, S32 max_icons, const LLColor4& bg)
{
    // Decide whether to emit the group-level divider. When the category
    // has descriptors with non-empty subcategories AND we're in the
    // unfiltered browsing path, the per-subgroup dividers serve as
    // section headers and the group divider becomes redundant noise
    // (the active group button already conveys group context).
    bool show_group_divider = true;
    if (mFilterPattern.empty() && category != FREQUENTLY_USED_CATEGORY)
    {
        const auto& category2Descr = LLEmojiDictionary::instance().getCategory2Descrs();
        auto c2d = category2Descr.find(category);
        if (c2d != category2Descr.end())
        {
            for (const LLEmojiDescriptor* descr : c2d->second)
            {
                if (!descr->Subcategory.empty())
                {
                    show_group_divider = false;
                    break;
                }
            }
        }
    }

    if (show_group_divider)
    {
        const std::string title =
            category == FREQUENTLY_USED_CATEGORY ? getString("title_for_frequently_used") :
            isupper(category.front()) ? category : LLStringUtil::capitalize(category);
        LLEmojiGridDivider* div = new LLEmojiGridDivider(row_panel_params, title);
        mEmojiGrid->addPanel(div, true);
    }

    int icon_index = 0;
    LLEmojiGridRow* row = nullptr;

    // Track the active subcategory so we can emit a smaller divider on
    // every transition. Subdividers only fire in the unfiltered,
    // non-recents path — recents have no meaningful subgroup structure
    // and search results are better shown as one flat match list.
    std::string current_subcat;
    auto maybe_emit_subdivider = [&](const std::string& subcat)
    {
        if (subcat.empty() || subcat == current_subcat)
            return;
        current_subcat = subcat;
        // Reset row tracking so the next emoji starts on its own row
        // beneath the new sub-divider, even if the previous row was
        // mid-fill.
        icon_index = 0;
        row = nullptr;
        const std::string sub_title =
            (!subcat.empty() && isupper(subcat.front())) ? subcat : LLStringUtil::capitalize(subcat);
        LLEmojiGridDivider* sub_div = new LLEmojiGridDivider(row_panel_params, sub_title);
        mEmojiGrid->addPanel(sub_div, true);
    };

    if (mFilterPattern.empty())
    {
        const LLEmojiDictionary& dict = LLEmojiDictionary::instance();
        const LLEmojiDictionary::emoji2descr_map_t& emoji2descr = dict.getEmoji2Descr();
        LLEmojiSearchResult emoji { LLWString(), "", 0, 0 };
        if (category == FREQUENTLY_USED_CATEGORY)
        {
            for (const auto& code : sFrequentlyUsed)
            {
                // Same fallback as fillCategoryFrequentlyUsed: a recent
                // variant sequence resolves to its base descriptor for
                // tooltip / category context.
                const auto e2d = emoji2descr.find(code.first);
                const LLEmojiDescriptor* descr = (e2d != emoji2descr.end()) ? e2d->second : dict.getBaseFromVariant(code.first);
                if (descr && !descr->ShortCodes.empty())
                {
                    emoji.Character = code.first;
                    emoji.String = descr->ShortCodes.front();
                    createEmojiIcon(emoji, category, row_panel_params, row_list_params, icon_params,
                        icon_rect, max_icons, bg, row, icon_index);
                }
            }
        }
        else
        {
            const LLEmojiDictionary::cat2descrs_map_t& category2Descr = LLEmojiDictionary::instance().getCategory2Descrs();
            const LLEmojiDictionary::cat2descrs_map_t::const_iterator& c2d = category2Descr.find(category);
            if (c2d != category2Descr.end())
            {
                for (const LLEmojiDescriptor* descr : c2d->second)
                {
                    maybe_emit_subdivider(descr->Subcategory);
                    emoji.Character = descr->Character;
                    emoji.String = descr->ShortCodes.front();
                    createEmojiIcon(emoji, category, row_panel_params, row_list_params, icon_params,
                        icon_rect, max_icons, bg, row, icon_index);
                }
            }
        }
    }
    else
    {
        for (const LLEmojiSearchResult& emoji : emojis)
        {
            createEmojiIcon(emoji, category, row_panel_params, row_list_params, icon_params,
                icon_rect, max_icons, bg, row, icon_index);
        }
    }
}

void LLFloaterEmojiPicker::createEmojiIcon(LLEmojiSearchResult emoji,
    const std::string& category, const LLPanel::Params& row_panel_params, const LLUICtrl::Params& row_list_params,
    const LLPanel::Params& icon_params, const LLRect& icon_rect, S32 max_icons, const LLColor4& bg,
    LLEmojiGridRow*& row, int& icon_index)
{
    // If the global tone preference is set and this emoji has a tone-only
    // variant matching it, substitute the variant character + shortcode
    // before constructing the icon. The substitution is skipped when the
    // input is itself already a variant (e.g. came from recents).
    applyTonePreference(emoji);

    // Look up the (possibly post-substitution) descriptor so the icon
    // knows whether to show the variant-affordance dot.
    const LLEmojiDictionary& dict = LLEmojiDictionary::instance();
    const LLEmojiDescriptor* descr = dict.getDescriptorFromEmoji(emoji.Character);
    if (!descr)
        descr = dict.getBaseFromVariant(emoji.Character);

    // Place a new row each (max_icons) icons
    if (!(icon_index % max_icons))
    {
        row = new LLEmojiGridRow(row_panel_params, *(const LLScrollingPanelList::Params*)&row_list_params);
        mEmojiGrid->addPanel(row, true);
    }

    // Place a new icon to the current row
    LLEmojiGridIcon* icon = new LLEmojiGridIcon(icon_params, emoji);
    icon->setHasVariants(descr && !descr->Variants.empty());
    icon->setMouseEnterCallback([this](LLUICtrl* ctrl, const LLSD&) { onEmojiMouseEnter(ctrl); });
    icon->setMouseLeaveCallback([this](LLUICtrl* ctrl, const LLSD&) { onEmojiMouseLeave(ctrl); });
    icon->setMouseDownCallback([this](LLUICtrl* ctrl, S32, S32, MASK) { onEmojiMouseDown(ctrl); });
    icon->setMouseUpCallback([this](LLUICtrl* ctrl, S32, S32, MASK) { onEmojiMouseUp(ctrl); });
    icon->setRightClickCallback([this](LLEmojiGridIcon* i) { onIconRightClick(i); });
    icon->setBackgroundColor(bg);
    icon->setBackgroundOpaque(1);
    icon->setRect(icon_rect);
    row->mList->addPanel(icon, true);

    icon_index++;
}

void LLFloaterEmojiPicker::applyTonePreference(LLEmojiSearchResult& emoji) const
{
    S32 tone_pref = gSavedSettings.getS32("EmojiSkinTonePreference");
    if (tone_pref < 0 || tone_pref > 4)
        return;

    const LLEmojiDictionary& dict = LLEmojiDictionary::instance();
    // Already-variant input (e.g. recents) preserves the user's choice.
    if (dict.getBaseFromVariant(emoji.Character))
        return;

    const LLEmojiDescriptor* base = dict.getDescriptorFromEmoji(emoji.Character);
    if (!base || base->Variants.empty())
        return;

    const LLEmojiVariant* v = dict.findVariant(*base, (U8)(tone_pref + 1), -1);
    if (!v)
        return;

    emoji.Character = v->Character;
    if (!v->ShortCodes.empty())
        emoji.String = v->ShortCodes.front();
}

void LLFloaterEmojiPicker::showPreview(bool show)
{
    mDummy->setVisible(!show);
    mPreview->setVisible(show);
}

namespace {
// The five Fitzpatrick skin-tone modifier codepoints, used as button labels
// in the tone strip. Index 0..4 corresponds to tone values 1..5 in
// LLEmojiVariant::Tone (light → dark).
constexpr llwchar TONE_MODIFIER_CODEPOINTS[5] = {
    0x1F3FB, // light
    0x1F3FC, // medium-light
    0x1F3FD, // medium
    0x1F3FE, // medium-dark
    0x1F3FF, // dark
};
// Yellow circle as the "no preference" marker.
constexpr llwchar TONE_NONE_CODEPOINT = 0x1F7E1;
}

void LLFloaterEmojiPicker::buildToneStrip()
{
    if (!mToneStrip)
        return;

    // Six buttons: "no preference" + 5 tones. Construct them once with
    // a placeholder rect; layoutToneStripButtons() (called from here and
    // from dirtyRect on resize) sets the actual rects.
    LLButton::Params params;
    params.font = LLFontGL::getFontEmojiLarge();
    params.tab_stop = false;

    auto make_button = [&](S32 tone_value, llwchar glyph, const std::string& tooltip_key, const std::string& name)
    {
        params.name = name;
        LLButton* button = LLUICtrlFactory::create<LLButton>(params);
        button->setLabel(LLUIString(LLWString(1, glyph)));
        button->setToolTip(getString(tooltip_key));
        // Render the tone-modifier glyph in its own font color from the
        // start — without this the buttons inherit the disabled-grey
        // styling and the swatches look washed out until first hover.
        button->setUseFontColor(true);
        S32 captured_tone = tone_value;
        button->setClickedCallback([this, captured_tone](LLUICtrl*, const LLSD&) { onToneButtonClick(captured_tone); });
        mToneStrip->addChild(button);
    };

    // -1 = no preference; 0..4 = tone 1..5.
    make_button(-1, TONE_NONE_CODEPOINT,           "tooltip_tone_none", "tone_none");
    for (S32 i = 0; i < 5; ++i)
    {
        make_button(i, TONE_MODIFIER_CODEPOINTS[i],
                    "tooltip_tone_" + std::to_string(i + 1),
                    "tone_" + std::to_string(i + 1));
    }

    layoutToneStripButtons();
    refreshToneStripHighlight();
}

void LLFloaterEmojiPicker::layoutToneStripButtons()
{
    if (!mToneStrip)
        return;

    const S32 BUTTON_COUNT = 6;
    S32 strip_width = mToneStrip->getRect().getWidth();
    S32 strip_height = mToneStrip->getRect().getHeight();
    S32 button_width = strip_width / BUTTON_COUNT;
    mToneStripLastWidth = strip_width;

    auto place = [&](const std::string& name, S32 index)
    {
        if (LLButton* b = mToneStrip->findChild<LLButton>(name))
        {
            b->setRect(LLRect(button_width * index, strip_height,
                              button_width * (index + 1), 0));
        }
    };

    place("tone_none", 0);
    for (S32 i = 0; i < 5; ++i)
    {
        place("tone_" + std::to_string(i + 1), i + 1);
    }
}

void LLFloaterEmojiPicker::onToneButtonClick(S32 tone)
{
    gSavedSettings.setS32("EmojiSkinTonePreference", tone);
    // Drive the refresh directly rather than waiting on the setting's
    // commit signal — the indirect path proved unreliable when the
    // setting's previous value already matched (no signal fires) and
    // when the connection wired in postBuild raced with the floater's
    // initial fillEmojis pass.
    refreshToneStripHighlight();
    fillEmojis();
}

void LLFloaterEmojiPicker::refreshToneStripHighlight()
{
    if (!mToneStrip)
        return;
    S32 tone_pref = gSavedSettings.getS32("EmojiSkinTonePreference");

    auto highlight = [&](const std::string& name, bool on)
    {
        if (LLButton* b = mToneStrip->findChild<LLButton>(name))
        {
            b->setToggleState(on);
        }
    };

    highlight("tone_none", tone_pref < 0 || tone_pref > 4);
    for (S32 i = 0; i < 5; ++i)
    {
        highlight("tone_" + std::to_string(i + 1), tone_pref == i);
    }
}

void LLFloaterEmojiPicker::draw()
{
    LLFloater::draw();

    // Deferred flyout dismissal — commitVariant flagged it from inside a
    // child cell's mouse-up callback. Deleting the flyout (which would
    // delete that cell) mid-dispatch is UB, so we wait one frame.
    if (mVariantFlyoutPendingDismiss)
    {
        mVariantFlyoutPendingDismiss = false;
        dismissVariantFlyout();
    }

    // Long-press detection: if the mouse has been held over the same icon
    // for ~500 ms without releasing, open the variant flyout.
    static constexpr F32 LONG_PRESS_SECONDS = 0.5f;
    if (mLongPressIcon && mLongPressTimer.getElapsedTimeF32() > LONG_PRESS_SECONDS)
    {
        LLEmojiGridIcon* icon = mLongPressIcon;
        mLongPressIcon = nullptr;
        mLongPressFired = true;
        showVariantFlyout(icon);
    }
}

bool LLFloaterEmojiPicker::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // Click outside the variant flyout dismisses it. The flyout itself
    // is a child of the floater so clicks ON it don't reach this method.
    if (mVariantFlyout && !mVariantFlyout->getRect().pointInRect(x, y))
    {
        dismissVariantFlyout();
    }
    return LLFloater::handleMouseDown(x, y, mask);
}

void LLFloaterEmojiPicker::onIconRightClick(LLEmojiGridIcon* icon)
{
    // Right-click is the explicit "show me alternates" gesture. Long-press
    // hits the same code path via draw(); both end up here.
    mLongPressIcon = nullptr; // cancel any in-flight long-press timer
    showVariantFlyout(icon);
}

void LLFloaterEmojiPicker::showVariantFlyout(LLEmojiGridIcon* baseIcon)
{
    if (!baseIcon)
        return;

    dismissVariantFlyout(); // never have two open at once

    // Resolve the descriptor: the icon's character may itself be a
    // variant (when the global tone preference rewrote the base), so we
    // walk up to the base.
    const LLEmojiDictionary& dict = LLEmojiDictionary::instance();
    const LLEmojiDescriptor* descr = dict.getDescriptorFromEmoji(baseIcon->getChar());
    if (!descr)
        descr = dict.getBaseFromVariant(baseIcon->getChar());
    if (!descr || descr->Variants.empty())
        return;

    // Detect whether the base is a "tone-pair" base (couples kissing,
    // holding hands, etc.) — those entries carry Tone2 != 0 and don't
    // have meaningful gender/hair axes. They get a dedicated 5x5 grid
    // (Tone × Tone2) instead of the (Gender, Hair) × Tone layout that
    // single-tone bases use.
    const bool tone_pair_mode = std::any_of(descr->Variants.begin(), descr->Variants.end(),
        [](const LLEmojiVariant& v) { return v.Tone2 != 0; });

    // Each row of the flyout corresponds to a (Gender, Hair) bucket
    // (single-tone bases) OR to first-tone (tone-pair bases). Cells
    // within a row map to tones — column 0 is the "bare" entry for
    // single-tone rows, columns 1..5 are tones 1..5; tone-pair rows
    // use columns 1..5 directly for the second tone.
    struct Row
    {
        S8 gender = -1;                               // single-tone mode
        std::string hair;                             // single-tone mode
        U8 first_tone = 0;                            // tone-pair mode
        const LLEmojiVariant* tone_zero = nullptr;    // bare entry (single-tone, may be null)
        std::array<const LLEmojiVariant*, 5> tones {}; // index 0..4 → tone 1..5, may be null
    };

    std::vector<Row> rows;

    if (tone_pair_mode)
    {
        // 5 rows × 5 columns (Tone × Tone2). Each variant slots into
        // exactly one cell.
        for (U8 t = 1; t <= 5; ++t)
        {
            Row r;
            r.first_tone = t;
            rows.push_back(r);
        }
        for (const LLEmojiVariant& v : descr->Variants)
        {
            if (v.Tone < 1 || v.Tone > 5) continue;
            if (v.Tone2 < 1 || v.Tone2 > 5) continue;
            rows[v.Tone - 1].tones[v.Tone2 - 1] = &v;
        }
    }
    else
    {
        // Bucket by (Gender, Hair). Hair entries get their own rows so
        // a hair=red tone-3 doesn't collide with a non-hair tone-3 in
        // the same column. The base's own character occupies the
        // gender=-1, hair="" row's column 0.
        auto get_or_make_row = [&](S8 gender, const std::string& hair) -> Row&
        {
            for (Row& r : rows)
                if (r.gender == gender && r.hair == hair) return r;
            Row r;
            r.gender = gender;
            r.hair = hair;
            rows.push_back(r);
            return rows.back();
        };

        for (const LLEmojiVariant& v : descr->Variants)
        {
            Row& row = get_or_make_row(v.Gender, v.Hair);
            if (v.Tone == 0)
            {
                row.tone_zero = &v;
            }
            else if (v.Tone >= 1 && v.Tone <= 5)
            {
                row.tones[v.Tone - 1] = &v;
            }
        }
        // Stable order:
        //   1. (gender=-1, hair="")  — the base axis (top of the grid)
        //   2. By gender rank: person (2), woman (1), man (0), other
        //   3. Within a gender: hair="" first, then red, curly, white, bald
        auto hair_rank = [](const std::string& h) -> int {
            if (h.empty())     return 0;
            if (h == "red")    return 1;
            if (h == "curly")  return 2;
            if (h == "white")  return 3;
            if (h == "bald")   return 4;
            return 5;
        };
        auto gender_rank = [](S8 g) -> int {
            switch (g) {
            case -1: return 0;
            case 2:  return 1;
            case 1:  return 2;
            case 0:  return 3;
            default: return 4;
            }
        };
        std::sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b)
        {
            int ga = gender_rank(a.gender), gb = gender_rank(b.gender);
            if (ga != gb) return ga < gb;
            return hair_rank(a.hair) < hair_rank(b.hair);
        });
    }

    // 6 columns (none + 5 tones) × N rows. Tone-pair mode reuses the
    // same column count, leaving column 0 empty.
    const S32 CELL = 28;
    const S32 PADDING = 4;
    const S32 COLS = 6;
    S32 row_count = (S32)rows.size();
    S32 width  = PADDING * 2 + CELL * COLS;
    S32 height = PADDING * 2 + CELL * row_count;

    // Position the flyout next to the base icon. Prefer to the side
    // (right then left) so the flyout doesn't sit on top of the section
    // headers/dividers that live in the vertical gaps between grid rows.
    // Fall back to above-or-below if neither side fits.
    LLRect base_rect = baseIcon->calcScreenRect();
    LLRect floater_rect = calcScreenRect();
    S32 base_left   = base_rect.mLeft   - floater_rect.mLeft;
    S32 base_right  = base_rect.mRight  - floater_rect.mLeft;
    S32 base_top    = base_rect.mTop    - floater_rect.mBottom;
    S32 base_bottom = base_rect.mBottom - floater_rect.mBottom;
    S32 cy = (base_top + base_bottom) / 2;
    S32 cx = (base_left + base_right) / 2;

    S32 floater_w = getRect().getWidth();
    S32 floater_h = getRect().getHeight();
    S32 space_right = floater_w - base_right;
    S32 space_left  = base_left;
    const S32 GAP = 4;

    S32 left, bottom;
    if (space_right >= width + GAP)
    {
        left   = base_right + GAP;
        bottom = llclamp(cy - height / 2, 0, floater_h - height);
    }
    else if (space_left >= width + GAP)
    {
        left   = base_left - GAP - width;
        bottom = llclamp(cy - height / 2, 0, floater_h - height);
    }
    else
    {
        // Neither side fits — fall back to vertical placement above the
        // base when there's room there, otherwise below.
        S32 space_above = floater_h - base_top;
        S32 space_below = base_bottom;
        if (space_above >= height + GAP)
            bottom = base_top + GAP;
        else if (space_below >= height + GAP)
            bottom = base_bottom - GAP - height;
        else
            bottom = llclamp(base_top + GAP, 0, floater_h - height);
        left = llclamp(cx - width / 2, 0, floater_w - width);
    }

    LLPanel::Params panel_params;
    panel_params.name("variant_flyout");
    panel_params.background_visible(true);
    panel_params.background_opaque(true);
    // mouse_opaque so hover/click events don't fall through to the main
    // grid icons sitting underneath the flyout. Without this, the grid
    // icon under a flyout cell ALSO receives mouse-enter, fires
    // onEmojiMouseEnter → selectGridIcon, and overwrites the preview
    // (showing the base shortcode) plus highlights itself in the grid.
    panel_params.mouse_opaque(true);
    // Force fully opaque background — the theme's MenuDefaultBgColor often
    // ships with alpha < 1.0, which lets section headers and grid rows
    // bleed through the flyout.
    LLColor4 flyout_bg = LLUIColorTable::instance().getColor("MenuDefaultBgColor", LLColor4(0.f, 0.f, 0.f, 1.0f)).get();
    flyout_bg.setAlpha(1.f);
    panel_params.bg_opaque_color(flyout_bg);
    LLPanel* flyout = LLUICtrlFactory::create<LLPanel>(panel_params);
    flyout->setRect(LLRect(left, bottom + height, left + width, bottom));

    LLPanel::Params cell_params;
    cell_params.background_visible(false);

    static LLUIColor hover_color = LLUIColorTable::instance().getColor("MenuItemHighlightBgColor", LLColor4(0.75f, 0.75f, 0.75f, 1.0f));

    // Reset cell matrix; sized to the row/col grid even when some entries
    // stay nullptr (data gaps). Keyboard nav walks this.
    mFlyoutCells.assign(row_count, std::vector<LLEmojiGridIcon*>(COLS, nullptr));

    auto add_cell = [&](S32 col, S32 row_index, const LLWString& seq, const std::string& shortcode)
    {
        if (seq.empty())
            return; // skip — empty cell, preserves grid alignment without rendering

        LLEmojiSearchResult sr(seq, shortcode, 0, 0);
        LLEmojiGridIcon* cell = new LLEmojiGridIcon(cell_params, sr);
        // Y coordinates are bottom-up; topmost row should sit at the top
        // of the flyout.
        S32 cell_left   = PADDING + col * CELL;
        S32 cell_right  = cell_left + CELL;
        S32 cell_top    = height - PADDING - row_index * CELL;
        S32 cell_bottom = cell_top - CELL;
        cell->setRect(LLRect(cell_left, cell_top, cell_right, cell_bottom));
        cell->setBackgroundColor(hover_color);
        cell->setBackgroundOpaque(true);
        cell->setMouseEnterCallback([this](LLUICtrl* c, const LLSD&)
        {
            if (auto* p = dynamic_cast<LLEmojiGridIcon*>(c))
            {
                p->setBackgroundVisible(true);
                // Update the floater's preview pane so the variant's
                // shortcode + name show under the cursor — matches the
                // main-grid hover affordance via selectGridIcon().
                if (mPreview)
                    mPreview->setIcon(p);
            }
        });
        cell->setMouseLeaveCallback([](LLUICtrl* c, const LLSD&)
        {
            if (auto* p = dynamic_cast<LLEmojiGridIcon*>(c))
            {
                p->setBackgroundVisible(false);
                // Don't touch the preview here. The framework fires
                // onMouseEnter for the new view BEFORE onMouseLeave for the
                // old one — so when cursor moves cell A → cell B, B's
                // enter has already set the preview to B and a leave-side
                // setIcon would overwrite it back to a stale fallback.
                // The next mouse-enter (whichever view receives it) is
                // responsible for updating the preview.
            }
        });
        cell->setMouseUpCallback([this](LLUICtrl* c, S32, S32, MASK)
        {
            if (LLEmojiGridIcon* picked = dynamic_cast<LLEmojiGridIcon*>(c))
                commitVariant(picked->getChar());
        });
        flyout->addChild(cell);
        mFlyoutCells[row_index][col] = cell;
    };

    auto first_shortcode = [](const std::list<std::string>& codes) -> std::string
    {
        return codes.empty() ? std::string() : codes.front();
    };

    for (S32 ri = 0; ri < row_count; ++ri)
    {
        const Row& r = rows[ri];

        if (tone_pair_mode)
        {
            // Tone-pair: row index encodes the first tone, columns 1..5
            // encode the second tone. Column 0 stays empty.
            for (S32 t = 0; t < 5; ++t)
            {
                if (const LLEmojiVariant* v = r.tones[t])
                {
                    add_cell(t + 1, ri, v->Character, first_shortcode(v->ShortCodes));
                }
            }
        }
        else
        {
            // Column 0 — the "bare" (no-tone) entry for this (Gender, Hair)
            // bucket. For (gender=-1, hair="") that's the base's own
            // character; for any other bucket it's the row's tone_zero
            // variant when present.
            if (r.gender == -1 && r.hair.empty())
            {
                add_cell(0, ri, descr->Character, first_shortcode(descr->ShortCodes));
            }
            else if (r.tone_zero)
            {
                add_cell(0, ri, r.tone_zero->Character, first_shortcode(r.tone_zero->ShortCodes));
            }

            for (S32 t = 0; t < 5; ++t)
            {
                if (const LLEmojiVariant* v = r.tones[t])
                {
                    add_cell(t + 1, ri, v->Character, first_shortcode(v->ShortCodes));
                }
            }
        }
    }

    addChild(flyout);
    mVariantFlyout = flyout;
    // Drop any stale main-grid highlights (from earlier keyboard nav or
    // a previously hovered icon). Without this, mFocusedIcon /
    // mHoveredIcon stay lit and the user sees two highlighted icons in
    // the grid: the base icon (pinned by us) and the leftover.
    if (mFocusedIcon && mFocusedIcon != baseIcon)
    {
        unselectGridIcon(mFocusedIcon);
    }
    if (mHoveredIcon && mHoveredIcon != baseIcon && mHoveredIcon != mFocusedIcon)
    {
        unselectGridIcon(mHoveredIcon);
    }
    // Pin the base icon as visually selected so the user can see which
    // emoji's variants they're browsing. onEmojiMouseEnter/Leave check
    // mFlyoutBaseIcon and skip the usual unselect path while the flyout
    // is up.
    mFlyoutBaseIcon = baseIcon;
    selectGridIcon(baseIcon);

    // Initial keyboard focus lands on the first valid cell, scanning
    // top-left to bottom-right. That's typically the gender=-1 row's
    // column 0 (the base "no preference" cell).
    mFlyoutFocusRow = -1;
    mFlyoutFocusCol = -1;
    for (S32 ri = 0; ri < (S32)mFlyoutCells.size() && mFlyoutFocusRow < 0; ++ri)
    {
        for (S32 ci = 0; ci < (S32)mFlyoutCells[ri].size(); ++ci)
        {
            if (mFlyoutCells[ri][ci])
            {
                setFlyoutFocus(ri, ci);
                break;
            }
        }
    }
}

void LLFloaterEmojiPicker::clearFlyoutFocus()
{
    if (mFlyoutFocusRow >= 0 && mFlyoutFocusRow < (S32)mFlyoutCells.size()
        && mFlyoutFocusCol >= 0 && mFlyoutFocusCol < (S32)mFlyoutCells[mFlyoutFocusRow].size())
    {
        if (LLEmojiGridIcon* prev = mFlyoutCells[mFlyoutFocusRow][mFlyoutFocusCol])
            prev->setBackgroundVisible(false);
    }
    mFlyoutFocusRow = -1;
    mFlyoutFocusCol = -1;
}

void LLFloaterEmojiPicker::setFlyoutFocus(S32 row, S32 col)
{
    clearFlyoutFocus();
    if (row < 0 || row >= (S32)mFlyoutCells.size())
        return;
    if (col < 0 || col >= (S32)mFlyoutCells[row].size())
        return;
    LLEmojiGridIcon* cell = mFlyoutCells[row][col];
    if (!cell)
        return;
    cell->setBackgroundVisible(true);
    if (mPreview)
        mPreview->setIcon(cell);
    mFlyoutFocusRow = row;
    mFlyoutFocusCol = col;
}

bool LLFloaterEmojiPicker::moveFlyoutFocus(S32 dRow, S32 dCol)
{
    if (mFlyoutCells.empty())
        return false;

    // Walk in the requested direction skipping nullptr (gap) cells. If
    // we hit a row/column boundary without finding a target, give up
    // and leave focus where it was — same shape as the main grid's
    // moveFocusedIcon* helpers.
    S32 row = std::max(mFlyoutFocusRow, 0);
    S32 col = std::max(mFlyoutFocusCol, 0);

    while (true)
    {
        row += dRow;
        col += dCol;
        if (row < 0 || row >= (S32)mFlyoutCells.size())
            return false;
        if (col < 0 || col >= (S32)mFlyoutCells[row].size())
            return false;
        if (mFlyoutCells[row][col])
        {
            setFlyoutFocus(row, col);
            return true;
        }
    }
}

bool LLFloaterEmojiPicker::commitFlyoutFocused()
{
    if (mFlyoutFocusRow < 0 || mFlyoutFocusCol < 0)
        return false;
    LLEmojiGridIcon* cell = mFlyoutCells[mFlyoutFocusRow][mFlyoutFocusCol];
    if (!cell)
        return false;
    commitVariant(cell->getChar());
    return true;
}

void LLFloaterEmojiPicker::dismissVariantFlyout()
{
    if (mVariantFlyout)
    {
        removeChild(mVariantFlyout);
        delete mVariantFlyout;
        mVariantFlyout = nullptr;
    }
    // Cells lived as children of the flyout panel — they're freed by
    // the panel destructor above. Clearing the matrix keeps the
    // pointers from being dereferenced by any stale focus state.
    mFlyoutCells.clear();
    mFlyoutFocusRow = -1;
    mFlyoutFocusCol = -1;

    // The base was pinned as selected while the flyout was open. Drop
    // the highlight unless it's also the currently hovered/focused icon
    // (in which case the normal hover/focus path keeps it lit).
    if (mFlyoutBaseIcon)
    {
        if (mFlyoutBaseIcon != mHoveredIcon && mFlyoutBaseIcon != mFocusedIcon)
        {
            unselectGridIcon(mFlyoutBaseIcon);
        }
        mFlyoutBaseIcon = nullptr;
    }

    // Restore the preview to whichever main-grid icon is currently
    // hovered or focused; otherwise blank it.
    if (mPreview)
    {
        LLEmojiGridIcon* restore = mHoveredIcon ? mHoveredIcon : mFocusedIcon;
        mPreview->setIcon(restore);
    }
}

void LLFloaterEmojiPicker::commitVariant(const LLWString& sequence)
{
    LLSD value(wstring_to_utf8str(sequence));
    setValue(value);
    onCommit();
    // Defer the actual deletion: this is being invoked from a child cell's
    // mouse-up callback, and dismissVariantFlyout would delete that very
    // cell mid-dispatch. draw() picks up mVariantFlyoutPendingDismiss next
    // frame and tears the panel down safely.
    mVariantFlyoutPendingDismiss = true;
    if (!mHint.empty() || !(gKeyboard->currentMask(true) & MASK_SHIFT))
    {
        hideFloater();
    }
}

void LLFloaterEmojiPicker::onGroupButtonClick(LLUICtrl* ctrl)
{
    if (LLButton* button = dynamic_cast<LLButton*>(ctrl))
    {
        if (button == mGroupButtons[mSelectedGroupIndex] || button->getToggleState())
            return;

        auto it = std::find(mGroupButtons.begin(), mGroupButtons.end(), button);
        if (it == mGroupButtons.end())
            return;

        selectEmojiGroup((U32)(it - mGroupButtons.begin()));
    }
}

void LLFloaterEmojiPicker::onGroupButtonMouseEnter(LLUICtrl* ctrl)
{
    if (LLButton* button = dynamic_cast<LLButton*>(ctrl))
    {
        button->setUseFontColor(true);
    }
}

void LLFloaterEmojiPicker::onGroupButtonMouseLeave(LLUICtrl* ctrl)
{
    if (LLButton* button = dynamic_cast<LLButton*>(ctrl))
    {
        button->setUseFontColor(button->getToggleState());
    }
}

void LLFloaterEmojiPicker::onEmojiMouseEnter(LLUICtrl* ctrl)
{
    if (LLEmojiGridIcon* icon = dynamic_cast<LLEmojiGridIcon*>(ctrl))
    {
        // While the variant flyout is up, the base icon is "locked" —
        // hovering other grid icons must not highlight them or hijack
        // the preview. The user is interacting with the flyout; only
        // the pinned base may show as selected in the grid.
        if (mVariantFlyout && icon != mFlyoutBaseIcon)
            return;

        if (mFocusedIcon && mFocusedIcon != icon && mFocusedIcon != mFlyoutBaseIcon && mFocusedIcon->isBackgroundVisible())
        {
            unselectGridIcon(mFocusedIcon);
        }

        if (mHoveredIcon && mHoveredIcon != icon && mHoveredIcon != mFlyoutBaseIcon)
        {
            unselectGridIcon(mHoveredIcon);
        }

        selectGridIcon(icon);

        mHoveredIcon = icon;
    }
}

void LLFloaterEmojiPicker::onEmojiMouseLeave(LLUICtrl* ctrl)
{
    if (LLEmojiGridIcon* icon = dynamic_cast<LLEmojiGridIcon*>(ctrl))
    {
        if (icon == mLongPressIcon)
            mLongPressIcon = nullptr;

        if (icon == mHoveredIcon)
        {
            // Keep the flyout's base icon highlighted while the flyout
            // is open even after the mouse wanders off.
            if (icon != mFocusedIcon && icon != mFlyoutBaseIcon)
            {
                unselectGridIcon(icon);
            }
            mHoveredIcon = nullptr;
        }

        if (!mHoveredIcon && mFocusedIcon && !mFocusedIcon->isBackgroundVisible())
        {
            selectGridIcon(mFocusedIcon);
        }
    }
}

void LLFloaterEmojiPicker::onEmojiMouseDown(LLUICtrl* ctrl)
{
    // Start the long-press timer; draw() polls it so the variant flyout
    // can fire after a short hold without depending on a global timer pool.
    if (LLEmojiGridIcon* icon = dynamic_cast<LLEmojiGridIcon*>(ctrl))
    {
        mLongPressIcon = icon;
        mLongPressFired = false;
        mLongPressTimer.reset();
    }

    if (getSoundFlags() & MOUSE_DOWN)
    {
        make_ui_sound("UISndClick");
    }
}

void LLFloaterEmojiPicker::onEmojiMouseUp(LLUICtrl* ctrl)
{
    // Cancel the pending long-press — a normal click was completed.
    mLongPressIcon = nullptr;

    // If the long-press already fired (variant flyout is up), the user
    // is just letting go of the hold; suppress the commit so the cell
    // doesn't double-trigger.
    if (mLongPressFired)
    {
        mLongPressFired = false;
        return;
    }

    if (getSoundFlags() & MOUSE_UP)
    {
        make_ui_sound("UISndClickRelease");
    }

    if (LLEmojiGridIcon* icon = dynamic_cast<LLEmojiGridIcon*>(ctrl))
    {
        LLSD value(wstring_to_utf8str(icon->getChar()));
        setValue(value);

        onCommit();

        if (!mHint.empty() || !(gKeyboard->currentMask(true) & MASK_SHIFT))
        {
            hideFloater();
        }
    }
}

void LLFloaterEmojiPicker::selectFocusedIcon()
{
    if (mFocusedIcon && mFocusedIcon != mHoveredIcon)
    {
        unselectGridIcon(mFocusedIcon);
    }

    // Both mFocusedIconRow and mFocusedIconCol should be already verified
    LLEmojiGridRow* row = dynamic_cast<LLEmojiGridRow*>(mEmojiGrid->getPanelList()[mFocusedIconRow]);
    mFocusedIcon = row ? dynamic_cast<LLEmojiGridIcon*>(row->mList->getPanelList()[mFocusedIconCol]) : nullptr;

    if (mFocusedIcon && !mHoveredIcon)
    {
        selectGridIcon(mFocusedIcon);
    }
}

bool LLFloaterEmojiPicker::moveFocusedIconUp()
{
    if (mHoveredIcon)
        return false;

    for (S32 i = mFocusedIconRow - 1; i >= 0; --i)
    {
        LLScrollingPanel* panel = mEmojiGrid->getPanelList()[i];
        LLEmojiGridRow* row = dynamic_cast<LLEmojiGridRow*>(panel);
        if (row && row->mList->getPanelList().size() > mFocusedIconCol)
        {
            mEmojiScroll->scrollToShowRect(row->getBoundingRect());
            mFocusedIconRow = i;
            selectFocusedIcon();
            return true;
        }
    }

    return false;
}

bool LLFloaterEmojiPicker::moveFocusedIconDown()
{
    if (mHoveredIcon)
        return false;

    auto rowCount = mEmojiGrid->getPanelList().size();
    for (size_t i = mFocusedIconRow + 1; i < rowCount; ++i)
    {
        LLScrollingPanel* panel = mEmojiGrid->getPanelList()[i];
        LLEmojiGridRow* row = dynamic_cast<LLEmojiGridRow*>(panel);
        if (row && row->mList->getPanelList().size() > mFocusedIconCol)
        {
            mEmojiScroll->scrollToShowRect(row->getBoundingRect());
            mFocusedIconRow = static_cast<S32>(i);
            selectFocusedIcon();
            return true;
        }
    }

    return false;
}

bool LLFloaterEmojiPicker::moveFocusedIconPrev()
{
    if (mHoveredIcon)
        return false;

    if (mFocusedIconCol > 0)
    {
        mFocusedIconCol--;
        selectFocusedIcon();
        return true;
    }

    for (S32 i = mFocusedIconRow - 1; i >= 0; --i)
    {
        LLScrollingPanel* panel = mEmojiGrid->getPanelList()[i];
        LLEmojiGridRow* row = dynamic_cast<LLEmojiGridRow*>(panel);
        if (row && row->mList->getPanelList().size())
        {
            mEmojiScroll->scrollToShowRect(row->getBoundingRect());
            mFocusedIconCol = static_cast<S32>(row->mList->getPanelList().size()) - 1;
            mFocusedIconRow = i;
            selectFocusedIcon();
            return true;
        }
    }

    return false;
}

bool LLFloaterEmojiPicker::moveFocusedIconNext()
{
    if (mHoveredIcon)
        return false;

    if (mFocusedIconRow < 0 || static_cast<size_t>(mFocusedIconRow) >= mEmojiGrid->getPanelList().size())
        return false;

    LLScrollingPanel* panel = mEmojiGrid->getPanelList()[mFocusedIconRow];
    LLEmojiGridRow* row = dynamic_cast<LLEmojiGridRow*>(panel);
    S32 colCount = row ? static_cast<S32>(row->mList->getPanelList().size()) : 0;
    if (mFocusedIconCol < colCount - 1)
    {
        mFocusedIconCol++;
        selectFocusedIcon();
        return true;
    }

    auto rowCount = mEmojiGrid->getPanelList().size();
    for (size_t i = mFocusedIconRow + 1; i < rowCount; ++i)
    {
        LLScrollingPanel* panel = mEmojiGrid->getPanelList()[i];
        LLEmojiGridRow* row = dynamic_cast<LLEmojiGridRow*>(panel);
        if (row && row->mList->getPanelList().size())
        {
            mEmojiScroll->scrollToShowRect(row->getBoundingRect());
            mFocusedIconCol = 0;
            mFocusedIconRow = static_cast<S32>(i);
            selectFocusedIcon();
            return true;
        }
    }

    return false;
}

void LLFloaterEmojiPicker::selectGridIcon(LLEmojiGridIcon* icon)
{
    icon->setBackgroundVisible(true);
    mPreview->setIcon(icon);
}

void LLFloaterEmojiPicker::unselectGridIcon(LLEmojiGridIcon* icon)
{
    icon->setBackgroundVisible(false);
    mPreview->setIcon(nullptr);
}

// virtual
bool LLFloaterEmojiPicker::handleKey(KEY key, MASK mask, bool called_from_parent)
{
    // While the variant flyout is up, arrow keys + Enter + Esc drive it
    // instead of the main grid. Esc dismisses just the flyout (returns
    // focus to the main grid); the next Esc still hides the floater.
    if (mVariantFlyout)
    {
        if (mask == MASK_NONE)
        {
            switch (key)
            {
            case KEY_UP:    return moveFlyoutFocus(-1,  0) || true;
            case KEY_DOWN:  return moveFlyoutFocus( 1,  0) || true;
            case KEY_LEFT:  return moveFlyoutFocus( 0, -1) || true;
            case KEY_RIGHT: return moveFlyoutFocus( 0,  1) || true;
            case KEY_RETURN:
                commitFlyoutFocused();
                return true;
            case KEY_ESCAPE:
                 dismissVariantFlyout();
                return true;
            default:
                break;
            }
        }
    }

    if (mask == MASK_NONE)
    {
        switch (key)
        {
        case KEY_UP:
            moveFocusedIconUp();
            return true;
        case KEY_DOWN:
            moveFocusedIconDown();
            return true;
        case KEY_LEFT:
            moveFocusedIconPrev();
            return true;
        case KEY_RIGHT:
            moveFocusedIconNext();
            return true;
        case KEY_ESCAPE:
            hideFloater();
            return true;
        }
    }

    if (mask == MASK_ALT)
    {
        switch (key)
        {
        case KEY_LEFT:
            selectEmojiGroup(static_cast<U32>((mSelectedGroupIndex + mFilteredEmojis.size()) % mGroupButtons.size()));
            return true;
        case KEY_RIGHT:
            selectEmojiGroup(static_cast<U32>((mSelectedGroupIndex + 1) % mGroupButtons.size()));
            return true;
        case KEY_DOWN:
            // Combobox-like "expand the dropdown" gesture: open the
            // variant flyout for the cell the user is currently aiming
            // at. The mouse-hovered cell wins when it's set so a user
            // mid-mouse-hover-then-Alt-Down gets the cell under the
            // cursor; we fall back to the keyboard-focused cell when
            // the mouse isn't over the grid.
            if (LLEmojiGridIcon* target = mHoveredIcon ? mHoveredIcon : mFocusedIcon)
            {
                showVariantFlyout(target);
            }
            return true;
        default:
            break;
        }
    }

    if (key == KEY_RETURN)
    {
        U64 time = totalTime();
        // <Shift+Return> comes twice for unknown reason
        if (mFocusedIcon && (time - mRecentReturnPressedMs > 100000)) // Min interval 0.1 sec.
        {
            onEmojiMouseDown(mFocusedIcon);
            onEmojiMouseUp(mFocusedIcon);
        }
        mRecentReturnPressedMs = time;
        return true;
    }

    if (mHint.empty())
    {
        if (key >= 0x20 && key < 0x80)
        {
            if (!mEmojiGrid->getPanelList().empty())
            {
                if (mFilterPattern.empty())
                {
                    mFilterPattern = ":";
                }
                mFilterPattern += (char)key;
                initialize();
            }
            return true;
        }
        else if (key == KEY_BACKSPACE)
        {
            if (!mFilterPattern.empty())
            {
                mFilterPattern.pop_back();
                if (mFilterPattern == ":")
                {
                    mFilterPattern.clear();
                }
                initialize();
            }
            return true;
        }
    }

    return super::handleKey(key, mask, called_from_parent);
}

// virtual
void LLFloaterEmojiPicker::goneFromFront()
{
    hideFloater();
}

void LLFloaterEmojiPicker::hideFloater() const
{
    LLEmojiHelper::instance().hideHelper(nullptr, true);
}

// static
std::list<LLWString>& LLFloaterEmojiPicker::getRecentlyUsed()
{
    loadState();
    return sRecentlyUsed;
}

// static
void LLFloaterEmojiPicker::onEmojiUsed(const LLWString& emoji)
{
    if (emoji.empty())
        return;

    // Update sRecentlyUsed
    auto itr = std::find(sRecentlyUsed.begin(), sRecentlyUsed.end(), emoji);
    if (itr == sRecentlyUsed.end())
    {
        sRecentlyUsed.push_front(emoji);
    }
    else if (itr != sRecentlyUsed.begin())
    {
        sRecentlyUsed.erase(itr);
        sRecentlyUsed.push_front(emoji);
    }

    // Increment and reorder sFrequentlyUsed
    auto itf = sFrequentlyUsed.begin();
    while (itf != sFrequentlyUsed.end())
    {
        if (itf->first == emoji)
        {
            itf->second++;
            while (itf != sFrequentlyUsed.begin())
            {
                auto prior = itf;
                prior--;
                if (prior->second > itf->second)
                    break;
                prior->swap(*itf);
                itf = prior;
            }
            break;
        }
        itf++;
    }
    // Append new if not found
    if (itf == sFrequentlyUsed.end())
    {
        // Insert before others with count == 1
        while (itf != sFrequentlyUsed.begin())
        {
            auto prior = itf;
            prior--;
            if (prior->second > 1)
                break;
            itf = prior;
        }
        sFrequentlyUsed.insert(itf, std::make_pair(emoji, 1));
    }
}

// static
void LLFloaterEmojiPicker::loadState()
{
    if (!sStateFileName.empty())
        return; // Already loaded

    sStateFileName = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "emoji_floater_state.xml");

    llifstream file;
    file.open(sStateFileName.c_str());
    if (!file.is_open())
    {
        LL_WARNS() << "Emoji floater state file is missing or inaccessible: " << sStateFileName << LL_ENDL;
        return;
    }

    LLSD state;
    LLSDSerialize::fromXML(state, file);
    if (state.isUndefined())
    {
        LL_WARNS() << "Emoji floater state file is missing or ill-formed: " << sStateFileName << LL_ENDL;
        return;
    }

    // Entries are stored as raw UTF-8 separated by commas (and colon for the
    // count in the frequently-used list). Comma (0x2C) and colon (0x3A)
    // never appear inside emoji bytes — UTF-8 continuation bytes are >= 0x80
    // and emoji codepoints are all non-ASCII — so this is unambiguous. Old
    // files written as decimal codepoints have all-ASCII tokens; we detect
    // those by checking for non-ASCII content and silently drop legacy
    // entries. Users rebuild their history in a session or two.

    // Load and parse sRecentlyUsed
    std::string recentlyUsed = state[sKeyRecentlyUsed];
    std::vector<std::string> rtokens = LLStringUtil::getTokens(recentlyUsed, ",");
    int maxCountR = 20;
    for (const std::string& token : rtokens)
    {
        LLWString emoji = utf8str_to_wstring(token);
        // Drop legacy ASCII-decimal tokens and obvious garbage.
        if (emoji.empty() || emoji[0] < 0x80)
            continue;
        if (std::find(sRecentlyUsed.begin(), sRecentlyUsed.end(), emoji) == sRecentlyUsed.end())
        {
            sRecentlyUsed.push_back(std::move(emoji));
            if (!--maxCountR)
                break;
        }
    }

    // Load and parse sFrequentlyUsed
    std::string frequentlyUsed = state[sKeyFrequentlyUsed];
    std::vector<std::string> ftokens = LLStringUtil::getTokens(frequentlyUsed, ",");
    int maxCountF = 20;
    for (const std::string& token : ftokens)
    {
        // Split on the FIRST colon only — UTF-8 bytes of emoji never contain
        // 0x3A so there's no ambiguity, but getTokens would split on every
        // colon.
        const size_t colon = token.find(':');
        if (colon == std::string::npos || colon == 0 || colon + 1 >= token.size())
            continue;

        LLWString emoji = utf8str_to_wstring(token.substr(0, colon));
        if (emoji.empty() || emoji[0] < 0x80)
            continue;

        const U32 count = (U32)atoi(token.c_str() + colon + 1);
        if (!count)
            continue;

        auto it = std::find_if(sFrequentlyUsed.begin(), sFrequentlyUsed.end(),
            [&emoji](std::pair<LLWString, U32>& it) { return it.first == emoji; });
        if (it != sFrequentlyUsed.end())
        {
            it->second += count;
        }
        else
        {
            sFrequentlyUsed.push_back(std::make_pair(std::move(emoji), count));
            if (!--maxCountF)
                break;
        }
    }

    // Normalize by minimum
    if (!sFrequentlyUsed.empty())
    {
        U32 delta = sFrequentlyUsed.back().second - 1;
        for (auto& it : sFrequentlyUsed)
        {
            it.second = std::max((U32)0, it.second - delta);
        }
    }
}

// static
void LLFloaterEmojiPicker::saveState()
{
    if (sStateFileName.empty())
        return; // Not loaded

    if (LLAppViewer::instance()->isSecondInstance())
        return; // Not allowed

    LLSD state = LLSD::emptyMap();

    // Entries are serialised as raw UTF-8 separated by commas. See loadState
    // for the invariant that keeps comma/colon separators unambiguous.
    if (!sRecentlyUsed.empty())
    {
        U32 maxCount = 20;
        std::string recentlyUsed;
        for (const LLWString& emoji : sRecentlyUsed)
        {
            if (!recentlyUsed.empty())
                recentlyUsed += ",";
            recentlyUsed += wstring_to_utf8str(emoji);
            if (!--maxCount)
                break;
        }
        state[sKeyRecentlyUsed] = recentlyUsed;
    }

    if (!sFrequentlyUsed.empty())
    {
        U32 maxCount = 20;
        std::string frequentlyUsed;
        for (const auto& it : sFrequentlyUsed)
        {
            if (!frequentlyUsed.empty())
                frequentlyUsed += ",";
            char buffer[32];
            snprintf(buffer, sizeof(buffer), ":%u", (U32)it.second);
            frequentlyUsed += wstring_to_utf8str(it.first);
            frequentlyUsed += buffer;
            if (!--maxCount)
                break;
        }
        state[sKeyFrequentlyUsed] = frequentlyUsed;
    }

    llofstream stream(sStateFileName.c_str());
    LLSDSerialize::toPrettyXML(state, stream);
}
