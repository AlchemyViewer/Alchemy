/**
 * @file alfloaterfeedback.h
 * @brief Where the user writes feedback for the developers and sends it
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

#ifndef AL_ALFLOATERFEEDBACK_H
#define AL_ALFLOATERFEEDBACK_H

#include "alfeedback.h"
#include "llfloater.h"
#include "llframetimer.h"
#include "llimage.h"

#include <array>
#include <functional>

class ALTextView;
class LLButton;
class LLCheckBoxCtrl;
class LLLineEditor;
class LLLoadingIndicator;
class LLPanel;
class LLRadioGroup;
class LLTextBox;
class LLTextEditor;
class LLViewerTexture;

class ALFloaterFeedback final : public LLFloater
{
public:
    AL_VIEW_TYPE(ALFloaterFeedback, LLFloater);

    // The key may say what the report is about: kind ("problem", "idea",
    // "other"), associated_event_id, linked ("crash" or "freeze"),
    // linked_at (a date) and previous_log (true to tick it).
    explicit ALFloaterFeedback(const LLSD& key);

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;

    // Opens the floater with the menus out of the way of its screenshot.
    static void show(const LLSD& key = LLSD());

    // After a run that crashed or froze, asks the user what happened, and
    // opens the floater on the report that run was filed as.
    static void askAboutLastRun();

private:
    ~ALFloaterFeedback() override;

    struct AttachmentRow
    {
        LLCheckBoxCtrl* check = nullptr;
        bool touched = false;
    };

    static void onIdle(void* self);
    static void onResult(LLHandle<ALFloaterFeedback> handle, const ALFeedback::Result& result);

    ALFeedback::Kind kind() const;
    void reportChanged();
    void applyKey(const LLSD& key);
    void applyKindDefaults();
    void setDefault(AttachmentRow& row, bool on);
    void updateCounter();
    void updateControls();
    void updateLinked();
    void updateAvatarLabel();
    void updateLogSizes();

    void requestScreenshot();
    void captureScreenshot();

    void send(bool with_attachments);
    void showFailure(const ALFeedback::Result& result);
    void showStatus(const std::string& text, const std::string& first_label, std::function<void()> first,
                    const std::string& second_label = std::string(), std::function<void()> second = nullptr);
    void hideStatus();

    void viewText(const std::string& title_string, const std::string& text);
    void viewLog(const std::string& title_string, const std::string& path);
    void viewScreenshot();

    void loadDraft();
    void saveDraft();
    static void clearDraft();

    LLRadioGroup* mKind = nullptr;
    LLTextEditor* mMessage = nullptr;
    LLTextBox* mCounter = nullptr;
    LLTextBox* mLinkedText = nullptr;
    LLButton* mUnlinkButton = nullptr;
    LLView* mScreenshotPreview = nullptr;
    LLCheckBoxCtrl* mHideInterface = nullptr;
    LLButton* mRetakeButton = nullptr;
    LLButton* mViewScreenshotButton = nullptr;
    AttachmentRow mScreenshotRow;
    AttachmentRow mSessionLogRow;
    AttachmentRow mPreviousLogRow;
    AttachmentRow mSystemInfoRow;
    AttachmentRow mSettingsRow;
    LLTextBox* mSessionLogSize = nullptr;
    LLTextBox* mPreviousLogSize = nullptr;
    LLButton* mViewPreviousLogButton = nullptr;
    LLCheckBoxCtrl* mIncludeAvatar = nullptr;
    LLLineEditor* mEmail = nullptr;
    LLTextBox* mEmailHint = nullptr;
    bool mEmailShownValid = true;
    LLCheckBoxCtrl* mRememberEmail = nullptr;
    LLTextBox* mPrivacyText = nullptr;
    LLPanel* mStatusPanel = nullptr;
    LLTextBox* mStatusText = nullptr;
    std::array<LLButton*, 2> mStatusButtons{};
    std::array<std::function<void()>, 2> mStatusActions;
    LLLoadingIndicator* mSendingIndicator = nullptr;
    LLButton* mSendButton = nullptr;
    LLButton* mCancelButton = nullptr;

    LLPointer<LLImageRaw> mScreenshot;
    LLPointer<LLViewerTexture> mThumbnail;
    S32 mThumbnailWidth = 0;
    S32 mThumbnailHeight = 0;
    LLFrameTimer mScreenshotTimer;
    bool mScreenshotPending = false;
    bool mCouldTakeScreenshot = false;
    LLTextBox* mScreenshotNote = nullptr;

    std::string mAssociatedEventId;
    std::string mLinked;
    std::string mLinkedAt;
    // The id of the report as it was last sent, so sending it again is the
    // same report to the server.
    std::string mEventId;
    std::string mPreviousLogFile;

    LLFrameTimer mDraftTimer;
    bool mDraftDirty = false;
    // Set once the report has gone, so closing keeps no draft.
    bool mSent = false;
    // Set while the report as last sent is kept to go by itself.
    bool mQueued = false;
    // Whether the controls were last set for a report being sent.
    bool mShownBusy = false;
};

// One attachment as it will be sent, to read before sending.
class ALFloaterFeedbackPreview final : public LLFloater
{
public:
    AL_VIEW_TYPE(ALFloaterFeedbackPreview, LLFloater);

    explicit ALFloaterFeedbackPreview(const LLSD& key);

    bool postBuild() override;
    void draw() override;

    static void showText(const std::string& title, std::string_view text);
    static void showImage(const std::string& title, const LLPointer<LLImageRaw>& image);

private:
    ~ALFloaterFeedbackPreview() override = default;

    ALTextView* mText = nullptr;
    LLPointer<LLViewerTexture> mImage;
    S32 mImageWidth = 0;
    S32 mImageHeight = 0;
};

#endif // AL_ALFLOATERFEEDBACK_H
