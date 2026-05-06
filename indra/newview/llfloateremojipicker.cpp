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
        LLFontGL* font = LLFontGL::getFontEmojiLarge();
        if (mBegin)
        {
            LLWString text = mTitle.substr(0, mBegin);
            font->render(
                text.c_str(),                          // text
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
            F32 dx = font->getWidthF32(text.c_str());
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
            F32 dx = font->getWidthF32(text.c_str());
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

    // Re-render the grid when the tone preference changes so existing
    // open pickers reflect the new tone immediately.
    if (LLControlVariable* ctrl = gSavedSettings.getControl("EmojiSkinTonePreference").get())
    {
        mTonePrefConnection = ctrl->getCommitSignal()->connect(
            [this](LLControlVariable*, const LLSD&, const LLSD&)
            {
                refreshToneStripHighlight();
                fillEmojis(true);
            });
    }

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

        mGroups->setVisible(false);
        mFocusedIconRow = -1;
        mFocusedIconCol = -1;
        mFocusedIcon = nullptr;
        mHoveredIcon = nullptr;
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
    createGroupButton(params, rect, ALL_EMOJIS_IMAGE_INDEX);

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
            createGroupButton(params, rect, USED_EMOJIS_IMAGE_INDEX);
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

void LLFloaterEmojiPicker::createGroupButton(LLButton::Params& params, const LLRect& rect, llwchar emoji)
{
    LLButton* button = LLUICtrlFactory::create<LLButton>(params);
    button->setClickedCallback([this](LLUICtrl* ctrl, const LLSD&) { onGroupButtonClick(ctrl); });
    button->setMouseEnterCallback([this](LLUICtrl* ctrl, const LLSD&) { onGroupButtonMouseEnter(ctrl); });
    button->setMouseLeaveCallback([this](LLUICtrl* ctrl, const LLSD&) { onGroupButtonMouseLeave(ctrl); });

    button->setRect(rect);
    button->setTabStop(false);
    button->setLabel(LLUIString(LLWString(1, emoji)));
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

    mFocusedIconRow = 0;
    mFocusedIconCol = 0;
    mFocusedIcon = nullptr;
    mHoveredIcon = nullptr;
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
    // Place the category title
    std::string title =
        category == FREQUENTLY_USED_CATEGORY ? getString("title_for_frequently_used") :
        isupper(category.front()) ? category : LLStringUtil::capitalize(category);
    LLEmojiGridDivider* div = new LLEmojiGridDivider(row_panel_params, title);
    mEmojiGrid->addPanel(div, true);

    int icon_index = 0;
    LLEmojiGridRow* row = nullptr;

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

    // Six buttons: "no preference" + 5 tones. They sit in a single row
    // across the top of the picker. Width is divvied evenly so resizing
    // the floater keeps them aligned.
    const S32 BUTTON_COUNT = 6;
    S32 strip_width = mToneStrip->getRect().getWidth();
    S32 strip_height = mToneStrip->getRect().getHeight();
    S32 button_width = strip_width / BUTTON_COUNT;

    LLButton::Params params;
    params.font = LLFontGL::getFontEmojiLarge();
    params.tab_stop = false;

    auto make_button = [&](S32 tone_value, llwchar glyph, const std::string& tooltip_key, const std::string& name)
    {
        params.name = name;
        LLButton* button = LLUICtrlFactory::create<LLButton>(params);
        LLRect rect(button_width * (tone_value + 1), strip_height,
                    button_width * (tone_value + 2), 0);
        // tone_value -1..4 → button index 0..5
        button->setRect(rect);
        button->setLabel(LLUIString(LLWString(1, glyph)));
        button->setToolTip(getString(tooltip_key));
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

    refreshToneStripHighlight();
}

void LLFloaterEmojiPicker::onToneButtonClick(S32 tone)
{
    gSavedSettings.setS32("EmojiSkinTonePreference", tone);
    // The setting-change listener takes care of refreshing the grid +
    // tone-strip highlight, so we don't need to call them here directly.
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
            b->setUseFontColor(on);
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

    // Build a transient panel of LLEmojiGridIcons: one for the base ("no
    // preference") plus one per variant.
    LLPanel::Params panel_params;
    panel_params.name("variant_flyout");
    panel_params.background_visible(true);
    panel_params.background_opaque(true);
    panel_params.bg_opaque_color(LLUIColorTable::instance().getColor("MenuDefaultBgColor", LLColor4(0.f, 0.f, 0.f, 0.9f)));
    LLPanel* flyout = LLUICtrlFactory::create<LLPanel>(panel_params);

    const S32 CELL = 28;
    const S32 PADDING = 4;
    S32 cell_count = 1 + (S32)descr->Variants.size();
    S32 width = PADDING * 2 + CELL * cell_count;
    S32 height = PADDING * 2 + CELL;

    // Position the flyout above the base icon, clamped to the floater's
    // client area so it doesn't spill off-screen.
    LLRect base_rect = baseIcon->calcScreenRect();
    LLRect floater_rect = calcScreenRect();
    S32 cx = base_rect.mLeft + base_rect.getWidth() / 2 - floater_rect.mLeft;
    S32 cy = base_rect.mTop - floater_rect.mBottom + 4;
    S32 left = llclamp(cx - width / 2, 0, getRect().getWidth() - width);
    S32 bottom = llclamp(cy, 0, getRect().getHeight() - height);
    flyout->setRect(LLRect(left, bottom + height, left + width, bottom));

    LLPanel::Params cell_params;
    cell_params.background_visible(false);

    static LLUIColor hover_color = LLUIColorTable::instance().getColor("MenuItemHighlightBgColor", LLColor4(0.75f, 0.75f, 0.75f, 1.0f));

    auto add_cell = [&](S32 x_offset, const LLWString& seq, const std::string& shortcode)
    {
        LLEmojiSearchResult sr(seq, shortcode, 0, 0);
        LLEmojiGridIcon* cell = new LLEmojiGridIcon(cell_params, sr);
        cell->setRect(LLRect(x_offset, PADDING + CELL, x_offset + CELL, PADDING));
        cell->setBackgroundColor(hover_color);
        cell->setBackgroundOpaque(true);
        // Hover highlight — match the main-grid hover affordance. We use
        // a dedicated pair of callbacks here (rather than the floater's
        // onEmojiMouseEnter/Leave) so the flyout cells don't perturb the
        // floater's mFocusedIcon/mHoveredIcon tracking.
        cell->setMouseEnterCallback([](LLUICtrl* c, const LLSD&)
        {
            if (auto* p = dynamic_cast<LLEmojiGridIcon*>(c))
                p->setBackgroundVisible(true);
        });
        cell->setMouseLeaveCallback([](LLUICtrl* c, const LLSD&)
        {
            if (auto* p = dynamic_cast<LLEmojiGridIcon*>(c))
                p->setBackgroundVisible(false);
        });
        cell->setMouseUpCallback([this](LLUICtrl* c, S32, S32, MASK)
        {
            if (LLEmojiGridIcon* picked = dynamic_cast<LLEmojiGridIcon*>(c))
                commitVariant(picked->getChar());
        });
        flyout->addChild(cell);
    };

    S32 x = PADDING;
    add_cell(x, descr->Character,
             descr->ShortCodes.empty() ? std::string() : descr->ShortCodes.front());
    x += CELL;
    for (const LLEmojiVariant& v : descr->Variants)
    {
        add_cell(x, v.Character, v.ShortCodes.empty() ? std::string() : v.ShortCodes.front());
        x += CELL;
    }

    addChild(flyout);
    mVariantFlyout = flyout;
}

void LLFloaterEmojiPicker::dismissVariantFlyout()
{
    if (mVariantFlyout)
    {
        removeChild(mVariantFlyout);
        delete mVariantFlyout;
        mVariantFlyout = nullptr;
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
        if (mFocusedIcon && mFocusedIcon != icon && mFocusedIcon->isBackgroundVisible())
        {
            unselectGridIcon(mFocusedIcon);
        }

        if (mHoveredIcon && mHoveredIcon != icon)
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
            if (icon != mFocusedIcon)
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
