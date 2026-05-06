/**
 * @file llfloateremojipicker.h
 * @brief Header file for llfloateremojipicker
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

#ifndef LLFLOATEREMOJIPICKER_H
#define LLFLOATEREMOJIPICKER_H

#include "llfloater.h"
#include "llframetimer.h"

class LLEmojiGridRow;
class LLEmojiGridIcon;
struct LLEmojiDescriptor;
struct LLEmojiSearchResult;
struct LLEmojiVariant;

class LLFloaterEmojiPicker : public LLFloater
{
    using super = LLFloater;

public:
    // The callback function will be called with an emoji char.
    typedef std::function<void(llwchar)> pick_callback_t;
    typedef std::function<void ()> close_callback_t;

    LLFloaterEmojiPicker(const LLSD& key);

    virtual bool postBuild() override;
    virtual void draw() override;
    virtual void dirtyRect() override;
    virtual void goneFromFront() override;
    virtual bool handleMouseDown(S32 x, S32 y, MASK mask) override;

    // Called by LLEmojiGridIcon when it receives a right-click — pops the
    // variant flyout for that icon's descriptor (no-op if no variants).
    void onIconRightClick(LLEmojiGridIcon* icon);

    void hideFloater() const;

    static std::list<LLWString>& getRecentlyUsed();
    static void onEmojiUsed(const LLWString& emoji);

    static void loadState();
    static void saveState();

private:
    void initialize();
    void fillGroups();
    void fillCategoryFrequentlyUsed(std::map<std::string, std::vector<LLEmojiSearchResult>>& cats);
    void fillGroupEmojis(std::map<std::string, std::vector<LLEmojiSearchResult>>& cats, U32 index);
    void createGroupButton(LLButton::Params& params, const LLRect& rect, llwchar emoji);
    void resizeGroupButtons();
    void selectEmojiGroup(U32 index);
    void fillEmojis(bool fromResize = false);
    void fillEmojisCategory(const std::vector<LLEmojiSearchResult>& emojis,
        const std::string& category, const LLPanel::Params& row_panel_params, const LLUICtrl::Params& row_list_params,
        const LLPanel::Params& icon_params, const LLRect& icon_rect, S32 max_icons, const LLColor4& bg);
    void createEmojiIcon(LLEmojiSearchResult emoji,
        const std::string& category, const LLPanel::Params& row_panel_params, const LLUICtrl::Params& row_list_params,
        const LLPanel::Params& icon_params, const LLRect& icon_rect, S32 max_icons, const LLColor4& bg,
        LLEmojiGridRow*& row, int& icon_index);
    void showPreview(bool show);

    // Tone strip and variant flyout helpers.
    void buildToneStrip();
    void layoutToneStripButtons();
    void onToneButtonClick(S32 tone);
    void refreshToneStripHighlight();
    void showVariantFlyout(LLEmojiGridIcon* baseIcon);
    void dismissVariantFlyout();
    void commitVariant(const LLWString& sequence);
    // Keyboard nav over the 2D flyout grid. Returns true when the
    // event was consumed.
    bool moveFlyoutFocus(S32 dRow, S32 dCol);
    void setFlyoutFocus(S32 row, S32 col);
    void clearFlyoutFocus();
    bool commitFlyoutFocused();
    // Substitute a variant character into a search-result-shaped struct
    // when the global tone preference applies. Skips the substitution if
    // the input is already a variant (i.e. came from recents).
    void applyTonePreference(LLEmojiSearchResult& emoji) const;

    void onGroupButtonClick(LLUICtrl* ctrl);
    void onGroupButtonMouseEnter(LLUICtrl* ctrl);
    void onGroupButtonMouseLeave(LLUICtrl* ctrl);
    void onEmojiMouseEnter(LLUICtrl* ctrl);
    void onEmojiMouseLeave(LLUICtrl* ctrl);
    void onEmojiMouseDown(LLUICtrl* ctrl);
    void onEmojiMouseUp(LLUICtrl* ctrl);

    void selectFocusedIcon();
    bool moveFocusedIconUp();
    bool moveFocusedIconDown();
    bool moveFocusedIconPrev();
    bool moveFocusedIconNext();

    void selectGridIcon(LLEmojiGridIcon* icon);
    void unselectGridIcon(LLEmojiGridIcon* icon);

    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;
    virtual bool handleKey(KEY key, MASK mask, bool called_from_parent) override;

    class LLPanel* mGroups { nullptr };
    class LLPanel* mBadge { nullptr };
    class LLPanel* mToneStrip { nullptr };
    class LLPanel* mVariantFlyout { nullptr };
    bool mVariantFlyoutPendingDismiss { false };
    // 2D matrix of variant-flyout cells; nullptr entries are "gaps"
    // (e.g. man-astronaut has no tone-3 variant in the data). Used by
    // the keyboard nav to walk only valid cells.
    std::vector<std::vector<LLEmojiGridIcon*>> mFlyoutCells;
    S32 mFlyoutFocusRow { -1 };
    S32 mFlyoutFocusCol { -1 };
    class LLScrollContainer* mEmojiScroll { nullptr };
    class LLScrollingPanelList* mEmojiGrid { nullptr };
    class LLEmojiPreviewPanel* mPreview { nullptr };
    class LLTextBox* mDummy { nullptr };

    std::vector<S32> mFilteredEmojiGroups;
    std::vector<std::map<std::string, std::vector<LLEmojiSearchResult>>> mFilteredEmojis;
    std::vector<class LLButton*> mGroupButtons;

    std::string mHint;
    std::string mFilterPattern;
    U32 mSelectedGroupIndex { 0 };
    S32 mRecentMaxIcons { 0 };
    S32 mFocusedIconRow { 0 };
    S32 mFocusedIconCol { 0 };
    LLEmojiGridIcon* mFocusedIcon { nullptr };
    LLEmojiGridIcon* mHoveredIcon { nullptr };

    U64 mRecentReturnPressedMs { 0 };

    // Long-press detection: we record the icon under the mouse on
    // mouse-down and check elapsed time in draw(). On expiry the variant
    // flyout opens. Mouse-up or mouse-leave clears mLongPressIcon.
    // mLongPressFired guards the subsequent mouse-up from also committing
    // the base character — once the flyout has opened, the release is
    // just dismissing the hold, not picking the cell.
    LLFrameTimer mLongPressTimer;
    LLEmojiGridIcon* mLongPressIcon { nullptr };
    bool mLongPressFired { false };

    // Cached strip width so dirtyRect can detect a resize and re-flow
    // the tone-strip buttons without doing the work every frame.
    S32 mToneStripLastWidth { 0 };
};

#endif
