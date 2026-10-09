/**
 * @file alfloaterfeedback.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alfloaterfeedback.h"

#include "alfloaterfeedbackpreview.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "lldate.h"
#include "llfile.h"
#include "llfloaterreg.h"
#include "llfontgl.h"
#include "lllineeditor.h"
#include "llloadingindicator.h"
#include "llnotificationsutil.h"
#include "llpanel.h"
#include "llradiogroup.h"
#include "llrender2dutils.h"
#include "llstartup.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltrans.h"
#include "lluicolortable.h"
#include "llviewercontrol.h"
#include "llviewermenu.h"
#include "llviewertexture.h"
#include "llviewerwindow.h"
#include "llvoavatarself.h"
#include "llweb.h"
#include "rlvactions.h"

#include <fmt/format.h>

#include <cmath>

namespace
{
    // The long edge of the screenshot sent; a larger window is scaled down.
    constexpr S32 SCREENSHOT_MAX_EDGE = 2560;
    // A report that has gone holds the next one back this long.
    constexpr F64 SEND_HOLD_SECONDS = 30.0;
    // A pause in typing this long saves the draft.
    constexpr F32 DRAFT_SAVE_DELAY = 2.f;
    // How often the floater looks again at what it is not told about: the
    // login, RLVa's restrictions, the hold on sending.
    constexpr F32 STATE_CHECK_SECONDS = 0.25f;

    F64 sHeldUntil = 0.0;

    // A snapshot draws the frame again only in world: before that the
    // startup display swaps as it draws, and what is read back is the frame
    // already on screen, this floater and the menu that opened it included.
    bool can_take_screenshot()
    {
        return LLStartUp::getStartupState() >= STATE_STARTED;
    }

    // The logs name regions; while RLVa hides where the user is, they stay
    // out of a report.
    bool location_hidden()
    {
        return !RlvActions::canShowLocation();
    }

    S32 hold_seconds_left()
    {
        const F64 left = sHeldUntil - LLFrameTimer::getTotalSeconds();
        return left > 0.0 ? static_cast<S32>(std::ceil(left)) : 0;
    }

    std::string trimmed(const std::string& text)
    {
        std::string out(text);
        LLStringUtil::trim(out);
        return out;
    }

    std::string size_text(S64 bytes)
    {
        if (bytes < 1024 * 1024)
        {
            return fmt::format("{} KB", std::max<S64>(1, bytes / 1024));
        }
        return fmt::format("{:.1f} MB", static_cast<F64>(bytes) / (1024.0 * 1024.0));
    }

    void copy_text(const std::string& text)
    {
        LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size()));
    }

    std::string failure_reason(const ALFeedback::Result& result)
    {
        // A report kept to go later says so, rather than asking for a retry.
        if (result.disconnected)
        {
            return LLTrans::getString("AlchemyFeedbackDisconnectedQueued");
        }
        switch (result.outcome)
        {
            case ALFeedback::Outcome::RateLimited:
                return LLTrans::getString(result.queued ? "AlchemyFeedbackRateLimitedQueued"
                                                        : "AlchemyFeedbackRateLimited");
            case ALFeedback::Outcome::TooLarge:
                return LLTrans::getString("AlchemyFeedbackTooLarge");
            case ALFeedback::Outcome::Rejected:
                return LLTrans::getString("AlchemyFeedbackRejected");
            case ALFeedback::Outcome::Unreachable:
            default:
                return LLTrans::getString(result.queued ? "AlchemyFeedbackUnreachableQueued"
                                                        : "AlchemyFeedbackUnreachable");
        }
    }

    void notify_sent(const std::string& event_id)
    {
        LLSD args;
        args["REF"] = ALFeedback::reference(event_id);
        LLSD payload;
        payload["event_id"] = event_id;
        LLNotificationsUtil::add("AlchemyFeedbackSent", args, payload,
                                 [](const LLSD& notification, const LLSD& response)
                                 {
                                     if (LLNotificationsUtil::getSelectedOption(notification, response) == 0)
                                     {
                                         copy_text(notification["payload"]["event_id"].asString());
                                     }
                                 });
    }

    void notify_unsent(const ALFeedback::Result& result)
    {
        if (result.queued)
        {
            LLNotificationsUtil::add(result.disconnected ? "AlchemyFeedbackQueuedDisconnected"
                                                         : "AlchemyFeedbackQueued");
            return;
        }
        LLSD args;
        args["REASON"] = failure_reason(result);
        LLNotificationsUtil::add("AlchemyFeedbackFailed", args, LLSD(),
                                 [](const LLSD& notification, const LLSD& response)
                                 {
                                     if (LLNotificationsUtil::getSelectedOption(notification, response) == 0)
                                     {
                                         ALFloaterFeedback::show();
                                     }
                                 });
    }
}

ALFloaterFeedback::ALFloaterFeedback(const LLSD& key)
:   LLFloater(key)
{
}

ALFloaterFeedback::~ALFloaterFeedback()
{
    gIdleCallbacks.deleteFunction(onIdle, this);
}

bool ALFloaterFeedback::postBuild()
{
    mKind = getChild<LLRadioGroup>("kind");
    // A radio group starts with nothing chosen: a problem, until the key or
    // a draft says otherwise.
    mKind->setSelectedIndex(0);
    mKind->setCommitCallback(
        [this](LLUICtrl*, const LLSD&)
        {
            reportChanged();
            applyKindDefaults();
            updateControls();
        });

    mMessage = getChild<LLTextEditor>("message");
    mMessage->setKeystrokeCallback(
        [this](LLTextEditor*)
        {
            reportChanged();
            updateCounter();
            updateControls();
        });

    mCounter = getChild<LLTextBox>("counter");
    mCounterColor = mCounter->getColor();
    mLinkedText = getChild<LLTextBox>("linked_text");
    mUnlinkButton = getChild<LLButton>("unlink_btn");
    mUnlinkButton->setCommitCallback(
        [this](LLUICtrl*, const LLSD&)
        {
            reportChanged();
            mAssociatedEventId.clear();
            mLinked.clear();
            mLinkedAt.clear();
            mLinkedRunId.clear();
            // A report of its own takes its kind's attachments.
            mPreviousLogRow.touched = false;
            applyKindDefaults();
            updateLinked();
            updateControls();
        });

    mScreenshotPreview = getChild<LLView>("screenshot_preview");
    getChild<LLUICtrl>("screenshot_preview")->setMouseUpCallback([this](LLUICtrl*, S32, S32, MASK) { viewScreenshot(); });
    mScreenshotNote = getChild<LLTextBox>("screenshot_note");
    mScreenshotShows = getChild<LLTextBox>("screenshot_shows");
    mScreenshotRow.check = getChild<LLCheckBoxCtrl>("screenshot_check");
    mScreenshotRow.check->setCommitCallback(
        [this](LLUICtrl*, const LLSD&)
        {
            reportChanged();
            mScreenshotRow.touched = true;
            if (mScreenshotRow.check->get() && !mScreenshot)
            {
                requestScreenshot();
            }
            updateControls();
        });
    mHideInterface = getChild<LLCheckBoxCtrl>("hide_ui_check");
    mHideInterface->setCommitCallback(
        [this](LLUICtrl*, const LLSD&)
        {
            reportChanged();
            requestScreenshot();
            updateControls();
        });
    mRetakeButton = getChild<LLButton>("retake_btn");
    mRetakeButton->setCommitCallback(
        [this](LLUICtrl*, const LLSD&)
        {
            reportChanged();
            requestScreenshot();
            updateControls();
        });
    mViewScreenshotButton = getChild<LLButton>("view_screenshot_btn");
    mViewScreenshotButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { viewScreenshot(); });

    const std::array<std::pair<AttachmentRow*, const char*>, 4> rows = { {
        { &mSessionLogRow, "session_log_check" },
        { &mPreviousLogRow, "previous_log_check" },
        { &mSystemInfoRow, "system_info_check" },
        { &mSettingsRow, "settings_check" },
    } };
    for (const auto& [row_ptr, name] : rows)
    {
        AttachmentRow* row = row_ptr;
        row->check = getChild<LLCheckBoxCtrl>(name);
        row->check->setCommitCallback(
            [this, row](LLUICtrl*, const LLSD&)
            {
                reportChanged();
                row->touched = true;
                updateControls();
            });
    }
    mSessionLogSize = getChild<LLTextBox>("session_log_size");
    mPreviousLogSize = getChild<LLTextBox>("previous_log_size");
    mViewSessionLogButton = getChild<LLButton>("view_session_log_btn");
    mViewSessionLogButton->setCommitCallback([this](LLUICtrl*, const LLSD&)
                                             { viewLog("title_session_log", ALFeedback::sessionLogFile()); });
    mViewPreviousLogButton = getChild<LLButton>("view_previous_log_btn");
    mViewPreviousLogButton->setCommitCallback([this](LLUICtrl*, const LLSD&)
                                              { viewLog("title_previous_log", mPreviousLogFile); });
    getChild<LLButton>("view_system_info_btn")
        ->setCommitCallback([this](LLUICtrl*, const LLSD&)
                            { viewText("title_system_info", ALFeedback::systemInformation()); });
    getChild<LLButton>("view_settings_btn")
        ->setCommitCallback([this](LLUICtrl*, const LLSD&)
                            { viewText("title_settings", ALFeedback::changedSettings()); });

    mIncludeAvatar = getChild<LLCheckBoxCtrl>("include_avatar_check");
    mIncludeAvatar->setCommitCallback([this](LLUICtrl*, const LLSD&) { reportChanged(); });
    mEmail = getChild<LLLineEditor>("email");
    mEmail->setKeystrokeCallback(
        [this](LLLineEditor*, void*)
        {
            reportChanged();
            updateControls();
        },
        nullptr);
    mEmailHint = getChild<LLTextBox>("email_hint");
    mRememberEmail = getChild<LLCheckBoxCtrl>("remember_email_check");
    mRememberEmail->setCommitCallback(
        [this](LLUICtrl*, const LLSD&)
        {
            // Unticked, the address is forgotten now, not at the next send.
            if (!mRememberEmail->get())
            {
                ALFeedback::rememberEmail(std::string());
            }
        });
    if (mRememberEmail->get())
    {
        mEmail->setText(ALFeedback::rememberedEmail());
    }

    mPrivacyText = getChild<LLTextBox>("privacy_text");
    mStatusPanel = getChild<LLPanel>("status_panel");
    mStatusText = mStatusPanel->getChild<LLTextBox>("status_text");
    for (size_t i = 0; i < mStatusButtons.size(); ++i)
    {
        mStatusButtons[i] = mStatusPanel->getChild<LLButton>(i == 0 ? "status_action_1" : "status_action_2");
        mStatusButtons[i]->setCommitCallback(
            [this, i](LLUICtrl*, const LLSD&)
            {
                if (std::function<void()> action = mStatusActions[i])
                {
                    action();
                }
            });
    }

    getChild<LLButton>("whats_in_btn")
        ->setCommitCallback([this](LLUICtrl*, const LLSD&)
                            { ALFloaterFeedbackPreview::showProse(getString("title_whats_in"), getString("whats_in")); });
    mSendingIndicator = getChild<LLLoadingIndicator>("sending_indicator");
    mSendButton = getChild<LLButton>("send_btn");
    mSendButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { send(true); });
    setDefaultBtn(mSendButton);
    mDiscardButton = getChild<LLButton>("discard_btn");
    mDiscardButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { discard(); });
    getChild<LLButton>("close_btn")->setCommitCallback([this](LLUICtrl*, const LLSD&) { closeFloater(); });

    mQueueListener = LLEventPumps::instance().obtain(ALFeedback::QUEUE_PUMP).listen(
        "ALFloaterFeedback", [this](const LLSD& event) { return onQueueEvent(event); });
    gIdleCallbacks.addFunction(onIdle, this);
    return true;
}

void ALFloaterFeedback::onOpen(const LLSD& key)
{
    mPreviousLogFile = ALFeedback::previousLogFile();
    mCouldTakeScreenshot = can_take_screenshot();
    applyKey(key);
    if (trimmed(mMessage->getText()).empty())
    {
        loadDraft();
    }
    applyKindDefaults();
    updateLinked();
    updateAvatarLabel();
    updateLogSizes();
    updateCounter();
    if (!mStatusPanel->getVisible())
    {
        showQueuedStatus();
    }
    // The screen now, not as it was when the report was started.
    if (mScreenshotRow.check->get())
    {
        requestScreenshot();
    }
    updateControls();
    mMessage->setFocus(true);
}

void ALFloaterFeedback::onClose(bool app_quitting)
{
    // A report kept to go by itself is the outbox's: nothing is left here.
    if (mQueued)
    {
        resetReport();
        return;
    }
    saveDraft();
    mDraftDirty = false;
}

void ALFloaterFeedback::draw()
{
    LLFloater::draw();

    if (mThumbnail && mScreenshotRow.check->get() && !isMinimized())
    {
        LLRect rect;
        mScreenshotPreview->localRectToOtherView(mScreenshotPreview->getLocalRect(), &rect, this);
        const S32 x = rect.mLeft + (rect.getWidth() - mThumbnailWidth) / 2;
        const S32 y = rect.mBottom + (rect.getHeight() - mThumbnailHeight) / 2;
        const F32 alpha = getTransparencyType() == TT_ACTIVE ? 1.f : getCurrentTransparency();
        gl_draw_scaled_image(x, y, mThumbnailWidth, mThumbnailHeight, mThumbnail, LLColor4::white % alpha);
    }
}

bool ALFloaterFeedback::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_RETURN && mask == MASK_CONTROL)
    {
        if (mSendButton->getEnabled())
        {
            send(true);
        }
        return true;
    }
    return LLFloater::handleKeyHere(key, mask);
}

// static
void ALFloaterFeedback::show(const LLSD& key)
{
    if (gMenuHolder)
    {
        gMenuHolder->hideMenus();
    }
    LLFloaterReg::showInstance("feedback", key, true);
}

// static
void ALFloaterFeedback::onIdle(void* self_ptr)
{
    ALFloaterFeedback* self = static_cast<ALFloaterFeedback*>(self_ptr);

    if (self->mDraftDirty && !self->mQueued && self->mDraftTimer.getElapsedTimeF32() > DRAFT_SAVE_DELAY)
    {
        self->mDraftDirty = false;
        self->saveDraft();
    }
    if (!self->getVisible())
    {
        return;
    }

    // An inactive window draws nothing, so its snapshot would be a frame
    // already shown; the capture waits for the window to be back.
    static LLCachedControl<F32> screenshot_delay(gSavedSettings, "AlchemyFeedbackScreenshotDelay", 0.3f);
    if (self->mScreenshotPending && !self->isMinimized() && gViewerWindow->getActive()
        && self->mScreenshotTimer.getElapsedTimeF32() > screenshot_delay)
    {
        self->mScreenshotPending = false;
        if (can_take_screenshot())
        {
            self->captureScreenshot();
        }
        self->updateControls();
    }

    const bool can_screenshot = can_take_screenshot();
    if (can_screenshot != self->mCouldTakeScreenshot)
    {
        self->mCouldTakeScreenshot = can_screenshot;
        self->applyKindDefaults();
        self->updateControls();
    }

    if (ALFeedback::sending() != self->mShownBusy)
    {
        self->updateControls();
    }

    if (self->mStateCheckTimer.getElapsedTimeF32() > STATE_CHECK_SECONDS)
    {
        self->mStateCheckTimer.reset();
        const bool logged_in = isAgentAvatarValid();
        if (logged_in != self->mShownLoggedIn)
        {
            self->updateAvatarLabel();
            self->updateControls();
        }
        else if (location_hidden() != self->mShownLocationHidden || hold_seconds_left() != self->mShownHoldSeconds)
        {
            self->updateControls();
        }
    }
}

bool ALFloaterFeedback::onQueueEvent(const LLSD& event)
{
    const std::string event_id = event["event_id"].asString();
    if (!mQueued || event_id.empty() || event_id != mEventId)
    {
        return false;
    }
    if (event["sent"].asBoolean())
    {
        resetReport();
        showStatus(getString("kept_sent"));
    }
    else
    {
        // Given up: the report is this floater's again, to send or not.
        mQueued = false;
        mLastResult.reset();
        mEventId.clear();
        mDraftDirty = true;
        mDraftTimer.reset();
        showStatus(getString("kept_given_up"), getString("action_try_again"), [this]() { send(true); });
    }
    updateControls();
    return false;
}

void ALFloaterFeedback::reportChanged()
{
    // Changed, it is another report under an id of its own: sent again with
    // the old one, the server would keep what it already had. A copy kept to
    // go by itself is the version the user is replacing, and what was said
    // about the last send no longer holds.
    if (mQueued)
    {
        ALFeedback::discardQueued(mEventId);
        mQueued = false;
    }
    if (!ALFeedback::sending() && mStatusPanel->getVisible())
    {
        hideStatus();
    }
    mLastResult.reset();
    mEventId.clear();
    mDraftDirty = true;
    mDraftTimer.reset();
}

void ALFloaterFeedback::resetReport()
{
    mMessage->setText(LLStringUtil::null);
    mKind->setSelectedIndex(0);
    mEventId.clear();
    mAssociatedEventId.clear();
    mLinked.clear();
    mLinkedAt.clear();
    mLinkedRunId.clear();
    mQueued = false;
    mLastResult.reset();
    mScreenshot = nullptr;
    mThumbnail = nullptr;
    mScreenshotPending = false;
    mScreenshotFailed = false;
    for (AttachmentRow* row : { &mScreenshotRow, &mSessionLogRow, &mPreviousLogRow, &mSystemInfoRow, &mSettingsRow })
    {
        row->touched = false;
    }
    mDraftDirty = false;
    hideStatus();
    applyKindDefaults();
    updateLinked();
    updateCounter();
    updateControls();
}

void ALFloaterFeedback::discard()
{
    if (!mEventId.empty() && (mQueued || ALFeedback::queued(mEventId)))
    {
        ALFeedback::discardQueued(mEventId);
    }
    ALFeedback::clearDraft();
    resetReport();
}

ALFeedback::Kind ALFloaterFeedback::kind() const
{
    return ALFeedback::kindFromName(mKind->getValue().asString());
}

void ALFloaterFeedback::applyKey(const LLSD& key)
{
    if (key.has("kind"))
    {
        mKind->setSelectedByValue(key["kind"], true);
    }
    if (key.has("associated_event_id") || key.has("linked"))
    {
        reportChanged();
        const std::string associated = key["associated_event_id"].asString();
        mAssociatedEventId = ALFeedback::validEventId(associated) ? associated : std::string();
        const std::string linked = key["linked"].asString();
        mLinked = (linked == "crash" || linked == "freeze") ? linked : std::string();
        mLinkedAt = mLinked.empty() ? std::string() : key["linked_at"].asString();
        mLinkedRunId = mLinked.empty() ? std::string() : key["linked_run_id"].asString();
    }
    if (key.has("previous_log"))
    {
        mPreviousLogRow.check->set(key["previous_log"].asBoolean() && !mPreviousLogFile.empty());
        mPreviousLogRow.touched = true;
    }
}

void ALFloaterFeedback::setDefault(AttachmentRow& row, bool on)
{
    if (!row.touched)
    {
        row.check->set(on);
    }
}

void ALFloaterFeedback::applyKindDefaults()
{
    const bool problem = kind() == ALFeedback::Kind::Problem;
    // About a crash, this session is the one that started after it, and the
    // screen is not what went wrong.
    const bool about_crash = !mLinked.empty();
    setDefault(mScreenshotRow, problem && !about_crash && can_take_screenshot());
    setDefault(mSessionLogRow, problem && !about_crash);
    setDefault(mPreviousLogRow, about_crash && !mPreviousLogFile.empty());
    setDefault(mSystemInfoRow, problem);
    setDefault(mSettingsRow, problem);

    static const std::array<const char*, 3> placeholders = { "placeholder_problem", "placeholder_idea",
                                                              "placeholder_other" };
    mMessage->setLabel(getString(placeholders[static_cast<size_t>(kind())]));

    if (mScreenshotRow.check->get() && !mScreenshot)
    {
        requestScreenshot();
    }
}

void ALFloaterFeedback::updateCounter()
{
    const size_t count = utf8str_codepoint_count(mMessage->getText());
    LLStringUtil::format_map_t args;
    args["[COUNT]"] = std::to_string(count);
    args["[MAX]"] = std::to_string(ALFeedback::MESSAGE_MAX_CHARS);
    mCounter->setText(getString("counter", args));
    mCounter->setColor(count > ALFeedback::MESSAGE_MAX_CHARS
                           ? LLUIColorTable::instance().getColor("AlertCautionTextColor")
                           : mCounterColor);
}

void ALFloaterFeedback::updateControls()
{
    const bool busy = ALFeedback::sending();
    const bool logged_in = isAgentAvatarValid();
    const bool hide_logs = location_hidden();
    const S32 hold = hold_seconds_left();
    mShownBusy = busy;
    mShownLoggedIn = logged_in;
    mShownLocationHidden = hide_logs;
    mShownHoldSeconds = hold;

    const size_t length = utf8str_codepoint_count(trimmed(mMessage->getText()));
    const std::string email = trimmed(mEmail->getText());
    const bool email_ok = email.empty() || ALFeedback::plausibleEmail(email);
    if (!mCouldTakeScreenshot && mScreenshotRow.check->get())
    {
        mScreenshotRow.check->set(false);
    }
    const bool screenshot = mScreenshotRow.check->get();

    // Send says why it cannot be pressed.
    std::string why_not;
    if (busy)
    {
        why_not = getString("send_busy");
    }
    else if (hold > 0)
    {
        why_not = getString("send_held", { { "[SECONDS]", std::to_string(hold) } });
    }
    else if (length < ALFeedback::MESSAGE_MIN_CHARS)
    {
        why_not = getString("send_too_short");
    }
    else if (length > ALFeedback::MESSAGE_MAX_CHARS)
    {
        why_not = getString("send_too_long");
    }
    else if (!email_ok)
    {
        why_not = getString("send_bad_email");
    }
    else if (screenshot && mScreenshotPending)
    {
        why_not = getString("screenshot_taking");
    }
    mSendButton->setEnabled(why_not.empty());
    mSendButton->setToolTip(why_not.empty() ? getString("send_tooltip") : why_not);
    mSendButton->setLabel(hold > 0 && !busy ? getString("label_send_held", { { "[SECONDS]", std::to_string(hold) } })
                                            : getString("label_send"));

    mSendingIndicator->setVisible(busy);
    if (busy)
    {
        mSendingIndicator->start();
    }
    else
    {
        mSendingIndicator->stop();
    }
    mDiscardButton->setEnabled(!busy && !trimmed(mMessage->getText()).empty());

    mKind->setEnabled(!busy);
    mMessage->setReadOnly(busy);
    mUnlinkButton->setEnabled(!busy);

    mScreenshotRow.check->setEnabled(!busy && mCouldTakeScreenshot);
    mScreenshotRow.check->setToolTip(mCouldTakeScreenshot ? getString("screenshot_tooltip")
                                                          : getString("screenshot_in_world"));
    // The empty box says why it is empty.
    const bool shown = screenshot && mThumbnail.notNull() && !mScreenshotPending;
    mScreenshotNote->setVisible(!shown);
    if (!shown)
    {
        const char* note = !mCouldTakeScreenshot ? "screenshot_in_world"
                           : !screenshot         ? "screenshot_none"
                           : mScreenshotPending  ? "screenshot_taking"
                           : mScreenshotFailed   ? "screenshot_failed"
                                                 : "screenshot_taking";
        mScreenshotNote->setText(getString(note));
    }
    mScreenshotShows->setVisible(screenshot && !mHideInterface->get());
    mHideInterface->setEnabled(!busy && screenshot);
    mRetakeButton->setEnabled(!busy && screenshot);
    mViewScreenshotButton->setEnabled(screenshot && mScreenshot.notNull());

    if (hide_logs)
    {
        mSessionLogRow.check->set(false);
        mPreviousLogRow.check->set(false);
    }
    const std::string& logs_tooltip = hide_logs ? getString("logs_location_hidden") : LLStringUtil::null;
    mSessionLogRow.check->setEnabled(!busy && !hide_logs);
    mSessionLogRow.check->setToolTip(logs_tooltip);
    mViewSessionLogButton->setEnabled(!hide_logs);
    mPreviousLogRow.check->setEnabled(!busy && !hide_logs && !mPreviousLogFile.empty());
    mPreviousLogRow.check->setToolTip(logs_tooltip);
    mViewPreviousLogButton->setEnabled(!hide_logs && !mPreviousLogFile.empty());
    mSystemInfoRow.check->setEnabled(!busy);
    mSettingsRow.check->setEnabled(!busy);

    mIncludeAvatar->setEnabled(!busy && logged_in);
    mEmail->setEnabled(!busy);
    mRememberEmail->setEnabled(!busy);
    if (email_ok != mEmailShownValid)
    {
        mEmailShownValid = email_ok;
        mEmailHint->setText(getString(email_ok ? "email_hint" : "email_invalid"));
    }
}

void ALFloaterFeedback::updateLinked()
{
    const bool linked = !mLinked.empty();
    mLinkedText->setVisible(linked);
    mUnlinkButton->setVisible(linked);
    if (!linked)
    {
        return;
    }

    const bool freeze = mLinked == "freeze";
    std::string text;
    if (mLinkedAt.empty())
    {
        text = getString(freeze ? "linked_freeze_undated" : "linked_crash_undated");
    }
    else
    {
        const LLSD when = LLSD().with("datetime", static_cast<S32>(LLDate(mLinkedAt).secondsSinceEpoch()));
        std::string date = getString("linked_date");
        LLStringUtil::format(date, when);
        std::string time = getString(gSavedSettings.getBOOL("Use24HourClock") ? "linked_time_24" : "linked_time_12");
        LLStringUtil::format(time, when);
        text = getString(freeze ? "linked_freeze" : "linked_crash", { { "[DATE]", date }, { "[TIME]", time } });
    }
    mLinkedText->setText(text);

    std::string tooltip = getString(freeze ? "linked_tooltip_freeze" : "linked_tooltip_crash");
    if (!mAssociatedEventId.empty())
    {
        tooltip += '\n';
        tooltip += getString("linked_tooltip_report", { { "[ID]", mAssociatedEventId } });
    }
    mLinkedText->setToolTip(tooltip);
    mUnlinkButton->setToolTip(getString(freeze ? "unlink_freeze" : "unlink_crash"));
}

void ALFloaterFeedback::updateAvatarLabel()
{
    if (isAgentAvatarValid())
    {
        mIncludeAvatar->setLabel(getString("include_avatar", { { "[NAME]", gAgentAvatarp->getFullname() } }));
    }
    else
    {
        mIncludeAvatar->setLabel(getString("include_avatar_logged_out"));
    }
}

void ALFloaterFeedback::updateLogSizes()
{
    const S64 tail = static_cast<S64>(ALFeedback::LOG_TAIL_BYTES);
    const std::string session_log = ALFeedback::sessionLogFile();
    const S64 session_size = session_log.empty() ? 0 : LLFile::size(session_log);
    mSessionLogSize->setText(size_text(std::min(session_size, tail)));
    if (mPreviousLogFile.empty())
    {
        mPreviousLogSize->setText(getString("no_previous_log"));
    }
    else
    {
        mPreviousLogSize->setText(size_text(std::min(LLFile::size(mPreviousLogFile), tail)));
    }
}

void ALFloaterFeedback::showQueuedStatus()
{
    if (mQueued)
    {
        return;
    }
    const size_t count = ALFeedback::queuedCount();
    if (count == 0)
    {
        return;
    }
    showStatus(getString(count == 1 ? "queued_one" : "queued_many", { { "[COUNT]", std::to_string(count) } }),
               getString("action_dont_send"),
               [this]()
               {
                   ALFeedback::discardAllQueued();
                   hideStatus();
               });
}

void ALFloaterFeedback::requestScreenshot()
{
    mScreenshotPending = true;
    mScreenshotTimer.reset();
}

void ALFloaterFeedback::captureScreenshot()
{
    S32 width = gViewerWindow->getWindowWidthRaw();
    S32 height = gViewerWindow->getWindowHeightRaw();
    const S32 edge = std::max(width, height);
    if (edge > SCREENSHOT_MAX_EDGE)
    {
        width = width * SCREENSHOT_MAX_EDGE / edge;
        height = height * SCREENSHOT_MAX_EDGE / edge;
    }
    const bool show_interface = !mHideInterface->get();

    // The snapshot draws the frame again, so this floater is out of it while
    // hidden; the L$ balance stays out whatever is shown.
    const bool had_focus = mMessage->hasFocus();
    LLPointer<LLImageRaw> raw = new LLImageRaw;
    setVisible(false);
    const bool taken = gViewerWindow->rawSnapshot(raw, width, height, true, false, show_interface, show_interface,
                                                  false, false, false);
    setVisible(true);
    if (had_focus)
    {
        mMessage->setFocus(true);
    }

    if (!taken)
    {
        LL_WARNS("Feedback") << "The screenshot could not be taken" << LL_ENDL;
        mScreenshot = nullptr;
        mThumbnail = nullptr;
        mScreenshotFailed = true;
        return;
    }
    mScreenshot = raw;
    mScreenshotFailed = false;

    // The thumbnail fills the preview's box, keeping the picture's shape.
    const LLRect box = mScreenshotPreview->getLocalRect();
    const F32 scale = std::min(static_cast<F32>(box.getWidth() - 2) / raw->getWidth(),
                               static_cast<F32>(box.getHeight() - 2) / raw->getHeight());
    mThumbnailWidth = std::max(1, ll_round(raw->getWidth() * scale));
    mThumbnailHeight = std::max(1, ll_round(raw->getHeight() * scale));
    LLPointer<LLImageRaw> thumbnail = new LLImageRaw(raw->getData(), raw->getWidth(), raw->getHeight(),
                                                     raw->getComponents());
    thumbnail->scale(mThumbnailWidth, mThumbnailHeight);
    mThumbnail = LLViewerTextureManager::getLocalTexture(thumbnail.get(), false);
}

void ALFloaterFeedback::send(bool with_attachments)
{
    if (ALFeedback::sending())
    {
        return;
    }

    ALFeedback::Report report;
    report.message.kind = kind();
    report.message.text = trimmed(mMessage->getText());
    report.message.email = trimmed(mEmail->getText());
    report.message.associatedEventId = mAssociatedEventId;
    report.includeUser = isAgentAvatarValid() && mIncludeAvatar->get();
    report.linked = mLinked;
    report.linkedRunId = mLinkedRunId;
    if (mEventId.empty())
    {
        mEventId = ALFeedback::newEventId();
    }
    report.eventId = mEventId;
    if (with_attachments)
    {
        if (mScreenshotRow.check->get())
        {
            report.screenshot = mScreenshot;
        }
        const bool logs = !location_hidden();
        report.sessionLog = logs && mSessionLogRow.check->get();
        report.previousLog = logs && mPreviousLogRow.check->get() && !mPreviousLogFile.empty();
        report.systemInfo = mSystemInfoRow.check->get();
        report.settings = mSettingsRow.check->get();
    }

    ALFeedback::rememberEmail(mRememberEmail->get() ? report.message.email : std::string());

    // Closing while it goes keeps the message, should it not arrive.
    saveDraft();
    mDraftDirty = false;
    if (!ALFeedback::send(std::move(report),
                          [handle = getDerivedHandle<ALFloaterFeedback>()](const ALFeedback::Result& result)
                          { onResult(handle, result); }))
    {
        return;
    }
    mLastResult.reset();
    showStatus(getString("sending"));
    updateControls();
}

// static
void ALFloaterFeedback::onResult(LLHandle<ALFloaterFeedback> handle, const ALFeedback::Result& result)
{
    ALFloaterFeedback* self = handle.get();
    if (result.outcome == ALFeedback::Outcome::Sent)
    {
        sHeldUntil = LLFrameTimer::getTotalSeconds() + SEND_HOLD_SECONDS;
        ALFeedback::clearDraft();
        notify_sent(result.eventId);
        if (self)
        {
            self->resetReport();
            self->closeFloater();
        }
        return;
    }

    // Kept to go by itself, the report is the outbox's, not the draft's.
    if (result.queued)
    {
        ALFeedback::clearDraft();
    }
    const bool seen = self && self->getVisible() && !self->isMinimized();
    if (self)
    {
        self->mQueued = result.queued;
        self->mLastResult = result;
        self->showFailure(result);
        self->updateControls();
    }
    if (!seen)
    {
        notify_unsent(result);
    }
}

void ALFloaterFeedback::showFailure(const ALFeedback::Result& result)
{
    const std::string reason = failure_reason(result);
    if (result.disconnected)
    {
        showStatus(reason);
        return;
    }
    switch (result.outcome)
    {
        case ALFeedback::Outcome::TooLarge:
            showStatus(reason, getString("action_without_attachments"), [this]() { send(false); });
            break;
        case ALFeedback::Outcome::Rejected:
            showStatus(reason, getString("action_github"),
                       []() { LLWeb::loadURLExternal(gSavedSettings.getString("ReportBugURL")); },
                       getString("action_copy_message"), [this]() { copy_text(trimmed(mMessage->getText())); });
            break;
        case ALFeedback::Outcome::RateLimited:
        case ALFeedback::Outcome::Unreachable:
        default:
            showStatus(reason, getString("action_try_again"), [this]() { send(true); });
            break;
    }
}

void ALFloaterFeedback::showStatus(const std::string& text, const std::string& first_label, std::function<void()> first,
                                   const std::string& second_label, std::function<void()> second)
{
    mStatusText->setText(text);
    const std::array<const std::string*, 2> labels = { &first_label, &second_label };
    mStatusActions = { std::move(first), std::move(second) };
    for (size_t i = 0; i < mStatusButtons.size(); ++i)
    {
        const bool shown = !labels[i]->empty() && mStatusActions[i];
        mStatusButtons[i]->setVisible(shown);
        if (shown)
        {
            mStatusButtons[i]->setLabel(*labels[i]);
        }
    }
    layoutStatus();
    mPrivacyText->setVisible(false);
    mStatusPanel->setVisible(true);
}

void ALFloaterFeedback::layoutStatus()
{
    // The buttons as wide as their labels, at the right; the text has the
    // rest of the line, all of it when there are no buttons.
    constexpr S32 PAD = 6;
    constexpr S32 GAP = 4;
    constexpr S32 BUTTON_MIN_WIDTH = 70;
    constexpr S32 LABEL_PAD = 16;
    S32 right = mStatusPanel->getRect().getWidth() - PAD;
    for (size_t i = mStatusButtons.size(); i-- > 0;)
    {
        LLButton* button = mStatusButtons[i];
        if (!button->getVisible())
        {
            continue;
        }
        const S32 width = std::max(BUTTON_MIN_WIDTH, button->getFont()->getWidth(button->getCurrentLabel().getString())
                                                          + LABEL_PAD);
        const LLRect old_rect = button->getRect();
        button->setShape(LLRect(right - width, old_rect.mTop, right, old_rect.mBottom));
        right -= width + GAP;
    }
    const LLRect text_rect = mStatusText->getRect();
    mStatusText->setShape(LLRect(PAD, text_rect.mTop, std::max(PAD + 1, right), text_rect.mBottom));
}

void ALFloaterFeedback::hideStatus()
{
    mStatusPanel->setVisible(false);
    mStatusActions = {};
    mPrivacyText->setVisible(true);
}

void ALFloaterFeedback::viewText(const std::string& title_string, const std::string& text)
{
    ALFloaterFeedbackPreview::showText(getString(title_string), text);
}

void ALFloaterFeedback::viewLog(const std::string& title_string, const std::string& path)
{
    // What the report would carry, hidden parts and all.
    if (!path.empty())
    {
        const bool include_user = isAgentAvatarValid() && mIncludeAvatar->get();
        viewText(title_string, ALFeedback::readLog(path, ALFeedback::hiddenFromLogs(include_user)));
    }
}

void ALFloaterFeedback::viewScreenshot()
{
    if (mScreenshot.notNull() && mScreenshotRow.check->get())
    {
        ALFloaterFeedbackPreview::showImage(getString("title_screenshot"), mScreenshot);
    }
}

void ALFloaterFeedback::loadDraft()
{
    const std::optional<ALFeedback::Draft> draft = ALFeedback::loadDraft();
    if (!draft)
    {
        return;
    }

    mMessage->setText(draft->message);
    mKind->setSelectedByValue(std::string(ALFeedback::kindName(draft->kind)), true);
    // A report the key tied to a crash keeps that; otherwise the draft's.
    if (mLinked.empty() && !draft->linked.empty())
    {
        mAssociatedEventId = draft->associatedEventId;
        mLinked = draft->linked;
        mLinkedAt = draft->linkedAt;
        mLinkedRunId = draft->linkedRunId;
    }
    if (draft->associatedEventId == mAssociatedEventId)
    {
        mEventId = draft->eventId;
    }
    // A run that ended while it went left the report kept, too: it is still
    // to go by itself, unless the user changes or discards it.
    mQueued = !mEventId.empty() && ALFeedback::queued(mEventId);
    showStatus(getString(mQueued ? "restored_queued" : "restored"), getString("action_discard"), [this]() { discard(); });
}

void ALFloaterFeedback::saveDraft()
{
    const std::string message = mMessage->getText();
    if (trimmed(message).empty())
    {
        ALFeedback::clearDraft();
        return;
    }

    ALFeedback::Draft draft;
    draft.kind = kind();
    draft.message = message;
    draft.eventId = mEventId;
    draft.associatedEventId = mAssociatedEventId;
    draft.linked = mLinked;
    draft.linkedAt = mLinkedAt;
    draft.linkedRunId = mLinkedRunId;
    ALFeedback::saveDraft(draft);
}
