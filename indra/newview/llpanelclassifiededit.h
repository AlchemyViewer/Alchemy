/**
 * @file llpanelclassifiededit.h
 * @brief LLPanelClassifiedEdit class definition
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Copyright (c) 2024, Cinder Roxley @ Second Life
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

#ifndef LL_LLPANELCLASSIFIEDEDIT_H
#define LL_LLPANELCLASSIFIEDEDIT_H

#include "llpanelclassified.h"

class LLPanelClassifiedEdit : public LLPanelClassifiedInfo
{
    LOG_CLASS(LLPanelClassifiedEdit);

  public:
    static LLPanelClassifiedEdit* create();

    virtual ~LLPanelClassifiedEdit();
    BOOL postBuild() override;
    void fillIn(const LLSD& key);
    void onOpen(const LLSD& key) override;
    void processProperties(void* data, EAvatarProcessorType type) override;
    BOOL isDirty() const override;
    void resetDirty() override;
    void setSaveCallback(const commit_signal_t::slot_type& cb);
    void setCancelCallback(const commit_signal_t::slot_type& cb);
    void resetControls() override;
    bool isNew() { return mIsNew; }
    bool isNewWithErrors() { return mIsNewWithErrors; }
    bool canClose();
    void draw() override;
    void stretchSnapshot();
    U32 getCategory();
    void setCategory(U32 category);
    U32 getContentType();
    void setContentType(U32 content_type);
    bool getAutoRenew();
    S32 getPriceForListing();

  protected:
    LLPanelClassifiedEdit();
    void sendUpdate();
    void enableVerbs(bool enable);
    void enableEditing(bool enable);
    void showEditing(bool show);
    std::string makeClassifiedName();
    void setPriceForListing(S32 price);
    U8 getFlags();
    bool isValidName();
    void notifyInvalidName();
    void onSetLocationClick();
    void onChange();
    void onSaveClick();
    void doSave();
    void onPublishFloaterPublishClicked();
    void onTexturePickerMouseEnter(LLUICtrl* ctrl);
    void onTexturePickerMouseLeave(LLUICtrl* ctrl);
    void onTextureSelected();

  private:
    bool mIsNew;
    bool mIsNewWithErrors;
    bool mCanClose;

    LLFloaterPublishClassified* mPublishFloater;

    commit_signal_t mSaveButtonClickedSignal;
};

#endif  // LL_LLPANELCLASSIFIEDEDIT_H
