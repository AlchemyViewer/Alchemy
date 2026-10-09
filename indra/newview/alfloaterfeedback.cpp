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

#include "alcrashreporter.h"
#include "altextview.h"
#include "llappviewer.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "lldate.h"
#include "lldir.h"
#include "llfile.h"
#include "llfloaterreg.h"
#include "lllineeditor.h"
#include "llloadingindicator.h"
#include "llnotificationsutil.h"
#include "llpanel.h"
#include "llradiogroup.h"
#include "llrender2dutils.h"
#include "llsdjson.h"
#include "llstartup.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewermenu.h"
#include "llviewertexture.h"
#include "llviewerwindow.h"
#include "llvoavatarself.h"

#include <fmt/format.h>

#include <cmath>

namespace
{
    // Fewer characters than this is not yet a message.
    constexpr size_t MESSAGE_MIN_CHARS = 4;
    // The long edge of the screenshot sent; a larger window is scaled down.
    constexpr S32 SCREENSHOT_MAX_EDGE = 2560;
    // A report that has gone holds the next one back this long.
    constexpr F64 SEND_HOLD_SECONDS = 30.0;
    // A pause in typing this long saves the draft.
    constexpr F32 DRAFT_SAVE_DELAY = 2.f;

    F64 sHeldUntil = 0.0;

    // A snapshot draws the frame again only in world: before that the
    // startup display swaps as it draws, and what is read back is the frame
    // already on screen, this floater and the menu that opened it included.
    bool can_take_screenshot()
    {
        return LLStartUp::getStartupState() >= STATE_STARTED;
    }

    std::string draft_file()
    {
        return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "feedback_draft.json");
    }

    std::string short_reference(const std::string& event_id)
    {
        return event_id.substr(0, 8);
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

    std::string failure_reason(const ALFeedback::Result& result)
    {
        switch (result.outcome)
        {
            case ALFeedback::Outcome::RateLimited:
            {
                LLStringUtil::format_map_t args;
                args["[MINUTES]"] = std::to_string(std::max(1, static_cast<S32>(std::ceil(result.retryAfter / 60.f))));
                return LLTrans::getString("AlchemyFeedbackRateLimited", args);
            }
            case ALFeedback::Outcome::TooLarge:
                return LLTrans::getString("AlchemyFeedbackTooLarge");
            case ALFeedback::Outcome::Rejected:
                return LLTrans::getString("AlchemyFeedbackRejected");
            case ALFeedback::Outcome::Unreachable:
            default:
                return LLTrans::getString("AlchemyFeedbackUnreachable");
        }
    }

    void notify_sent(const std::string& event_id)
    {
        LLSD args;
        args["REF"] = short_reference(event_id);
        LLSD payload;
        payload["event_id"] = event_id;
        LLNotificationsUtil::add("AlchemyFeedbackSent", args, payload,
                                 [](const LLSD& notification, const LLSD& response)
                                 {
                                     if (LLNotificationsUtil::getSelectedOption(notification, response) == 0)
                                     {
                                         const std::string id = notification["payload"]["event_id"].asString();
                                         LLClipboard::instance().copyToClipboard(id, 0, static_cast<S32>(id.size()));
                                     }
                                 });
    }

    void notify_failed(const ALFeedback::Result& result)
    {
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
            applyKindDefaults();
            updateControls();
            mDraftDirty = true;
            mDraftTimer.reset();
        });

    mMessage = getChild<LLTextEditor>("message");
    mMessage->setKeystrokeCallback(
        [this](LLTextEditor*)
        {
            // Changed, it is another report, and goes under an id of its own.
            mEventId.clear();
            updateCounter();
            updateControls();
            mDraftDirty = true;
            mDraftTimer.reset();
        });

    mCounter = getChild<LLTextBox>("counter");
    mLinkedText = getChild<LLTextBox>("linked_text");
    mUnlinkButton = getChild<LLButton>("unlink_btn");
    mUnlinkButton->setCommitCallback(
        [this](LLUICtrl*, const LLSD&)
        {
            mAssociatedEventId.clear();
            mLinked.clear();
            mLinkedAt.clear();
            mEventId.clear();
            updateLinked();
            mDraftDirty = true;
            mDraftTimer.reset();
        });

    mScreenshotPreview = getChild<LLView>("screenshot_preview");
    getChild<LLUICtrl>("screenshot_preview")->setMouseUpCallback([this](LLUICtrl*, S32, S32, MASK) { viewScreenshot(); });
    mScreenshotNote = getChild<LLTextBox>("screenshot_note");
    mScreenshotRow.check = getChild<LLCheckBoxCtrl>("screenshot_check");
    mScreenshotRow.check->setCommitCallback(
        [this](LLUICtrl*, const LLSD&)
        {
            mScreenshotRow.touched = true;
            if (mScreenshotRow.check->get() && !mScreenshot)
            {
                requestScreenshot();
            }
            updateControls();
        });
    mHideInterface = getChild<LLCheckBoxCtrl>("hide_ui_check");
    mHideInterface->setCommitCallback([this](LLUICtrl*, const LLSD&) { requestScreenshot(); });
    mRetakeButton = getChild<LLButton>("retake_btn");
    mRetakeButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { requestScreenshot(); });
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
                row->touched = true;
                updateControls();
            });
    }
    mSessionLogSize = getChild<LLTextBox>("session_log_size");
    mPreviousLogSize = getChild<LLTextBox>("previous_log_size");
    getChild<LLButton>("view_session_log_btn")
        ->setCommitCallback([this](LLUICtrl*, const LLSD&)
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
    mEmail = getChild<LLLineEditor>("email");
    mEmail->setKeystrokeCallback([this](LLLineEditor*, void*) { updateControls(); }, nullptr);
    mEmailHint = getChild<LLTextBox>("email_hint");
    mRememberEmail = getChild<LLCheckBoxCtrl>("remember_email_check");
    if (mRememberEmail->get())
    {
        mEmail->setText(gSavedSettings.getString("AlchemyFeedbackEmail"));
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
        ->setCommitCallback([this](LLUICtrl*, const LLSD&) { viewText("title_whats_in", getString("whats_in")); });
    mSendingIndicator = getChild<LLLoadingIndicator>("sending_indicator");
    mSendButton = getChild<LLButton>("send_btn");
    mSendButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { send(true); });
    mCancelButton = getChild<LLButton>("cancel_btn");
    mCancelButton->setCommitCallback([this](LLUICtrl*, const LLSD&) { closeFloater(); });

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
    updateControls();
    if (mScreenshotRow.check->get() && !mScreenshot)
    {
        requestScreenshot();
    }
    mMessage->setFocus(true);
}

void ALFloaterFeedback::onClose(bool app_quitting)
{
    if (!mSent)
    {
        saveDraft();
    }
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
void ALFloaterFeedback::askAboutLastRun()
{
    // The login screen is shown again after a failed login; once a launch.
    static bool asked = false;
    if (asked || !ALFeedback::available() || LLAppViewer::instance()->isSecondInstance())
    {
        return;
    }
    asked = true;

    std::string kind;
    switch (gLastExecEvent)
    {
        case LAST_EXEC_FROZE:
        case LAST_EXEC_LOGOUT_FROZE:
            kind = "freeze";
            break;
        case LAST_EXEC_LLERROR_CRASH:
        case LAST_EXEC_OTHER_CRASH:
        case LAST_EXEC_LOGOUT_CRASH:
        case LAST_EXEC_BAD_ALLOC:
            kind = "crash";
            break;
        default:
            // An unknown end is as often the task manager or a power cut.
            return;
    }

    LLSD key;
    key["kind"] = "problem";
    key["linked"] = kind;
    key["previous_log"] = true;
    if (const std::optional<ALCrashReporter::PreviousReport> report = ALCrashReporter::previousReport())
    {
        key["associated_event_id"] = report->eventId;
    }
    // When it happened: about when the previous run last wrote its log.
    const std::string previous_log = ALFeedback::previousLogFile();
    llstat status;
    if (!previous_log.empty() && LLFile::stat(previous_log, &status) == 0)
    {
        key["linked_at"] = LLDate(static_cast<F64>(status.st_mtime)).asString();
    }

    LLSD args;
    args["WHAT"] = LLTrans::getString(kind == "freeze" ? "AlchemyFeedbackFroze" : "AlchemyFeedbackCrashed");
    LLNotificationsUtil::add("AlchemyFeedbackAfterCrash", args, key,
                             [](const LLSD& notification, const LLSD& response)
                             {
                                 if (LLNotificationsUtil::getSelectedOption(notification, response) == 0)
                                 {
                                     ALFloaterFeedback::show(notification["payload"]);
                                 }
                             });
}

// static
void ALFloaterFeedback::onIdle(void* self_ptr)
{
    ALFloaterFeedback* self = static_cast<ALFloaterFeedback*>(self_ptr);

    // An inactive window draws nothing, so its snapshot would be a frame
    // already shown; the capture waits for the window to be back.
    static LLCachedControl<F32> screenshot_delay(gSavedSettings, "AbuseReportScreenshotDelay", 0.3f);
    if (self->mScreenshotPending && self->getVisible() && !self->isMinimized() && gViewerWindow->getActive()
        && self->mScreenshotTimer.getElapsedTimeF32() > screenshot_delay)
    {
        self->mScreenshotPending = false;
        if (can_take_screenshot())
        {
            self->captureScreenshot();
        }
    }

    const bool can_screenshot = can_take_screenshot();
    if (can_screenshot != self->mCouldTakeScreenshot)
    {
        self->mCouldTakeScreenshot = can_screenshot;
        self->applyKindDefaults();
        self->updateControls();
    }

    if (self->mDraftDirty && self->mDraftTimer.getElapsedTimeF32() > DRAFT_SAVE_DELAY)
    {
        self->mDraftDirty = false;
        self->saveDraft();
    }

    if (sHeldUntil > 0.0 && LLFrameTimer::getTotalSeconds() >= sHeldUntil)
    {
        sHeldUntil = 0.0;
        self->updateControls();
    }
}

ALFeedback::Kind ALFloaterFeedback::kind() const
{
    const std::string value = mKind->getValue().asString();
    if (value == "idea")
    {
        return ALFeedback::Kind::Idea;
    }
    if (value == "other")
    {
        return ALFeedback::Kind::Other;
    }
    return ALFeedback::Kind::Problem;
}

void ALFloaterFeedback::applyKey(const LLSD& key)
{
    if (key.has("kind"))
    {
        mKind->setSelectedByValue(key["kind"], true);
    }
    if (key.has("associated_event_id") || key.has("linked"))
    {
        mAssociatedEventId = key["associated_event_id"].asString();
        mLinked = key["linked"].asString();
        mLinkedAt = key["linked_at"].asString();
        mEventId.clear();
    }
    if (key["previous_log"].asBoolean() && !mPreviousLogFile.empty())
    {
        mPreviousLogRow.check->set(true);
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
    setDefault(mSystemInfoRow, true);
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
    LLStringUtil::format_map_t args;
    args["[COUNT]"] = std::to_string(utf8str_codepoint_count(mMessage->getText()));
    args["[MAX]"] = std::to_string(ALFeedback::MESSAGE_MAX_CHARS);
    mCounter->setText(getString("counter", args));
}

void ALFloaterFeedback::updateControls()
{
    const bool busy = ALFeedback::sending();
    const size_t length = utf8str_codepoint_count(trimmed(mMessage->getText()));
    const std::string email = trimmed(mEmail->getText());
    const bool email_ok = email.empty() || ALFeedback::plausibleEmail(email);
    const bool held = sHeldUntil > LLFrameTimer::getTotalSeconds();

    mSendButton->setEnabled(!busy && !held && email_ok && length >= MESSAGE_MIN_CHARS
                            && length <= ALFeedback::MESSAGE_MAX_CHARS);
    mSendingIndicator->setVisible(busy);
    if (busy)
    {
        mSendingIndicator->start();
    }
    else
    {
        mSendingIndicator->stop();
    }
    mCancelButton->setLabel(getString(busy ? "label_close" : "label_cancel"));

    mKind->setEnabled(!busy);
    mMessage->setReadOnly(busy);
    mUnlinkButton->setEnabled(!busy);
    if (!mCouldTakeScreenshot && mScreenshotRow.check->get())
    {
        mScreenshotRow.check->set(false);
    }
    const bool screenshot = mScreenshotRow.check->get();
    mScreenshotRow.check->setEnabled(!busy && mCouldTakeScreenshot);
    mScreenshotRow.check->setToolTip(mCouldTakeScreenshot ? LLStringUtil::null : getString("screenshot_in_world"));
    // The empty box says why it is empty.
    const bool shown = screenshot && mThumbnail.notNull();
    mScreenshotNote->setVisible(!shown);
    if (!shown)
    {
        mScreenshotNote->setText(getString(!mCouldTakeScreenshot ? "screenshot_in_world"
                                           : screenshot           ? "screenshot_taking"
                                                                  : "screenshot_none"));
    }
    mHideInterface->setEnabled(!busy && screenshot);
    mRetakeButton->setEnabled(!busy && screenshot);
    mViewScreenshotButton->setEnabled(screenshot && mScreenshot.notNull());
    mSessionLogRow.check->setEnabled(!busy);
    mPreviousLogRow.check->setEnabled(!busy && !mPreviousLogFile.empty());
    mViewPreviousLogButton->setEnabled(!mPreviousLogFile.empty());
    mSystemInfoRow.check->setEnabled(!busy);
    mSettingsRow.check->setEnabled(!busy);
    mIncludeAvatar->setEnabled(!busy);
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
        text = getString(freeze ? "linked_freeze" : "linked_crash");
        LLSD substitution;
        substitution["datetime"] = static_cast<S32>(LLDate(mLinkedAt).secondsSinceEpoch());
        LLStringUtil::format(text, substitution);
    }
    if (!mAssociatedEventId.empty())
    {
        LLStringUtil::format_map_t args;
        args["[REF]"] = short_reference(mAssociatedEventId);
        text += ' ';
        text += getString("linked_reference", args);
    }
    mLinkedText->setText(text);
    mLinkedText->setToolTip(mAssociatedEventId);
}

void ALFloaterFeedback::updateAvatarLabel()
{
    if (isAgentAvatarValid())
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = gAgentAvatarp->getFullname();
        mIncludeAvatar->setLabel(getString("include_avatar", args));
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
        updateControls();
        return;
    }
    mScreenshot = raw;

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
    updateControls();
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
    report.includeUser = mIncludeAvatar->get();
    if (report.includeUser && isAgentAvatarValid())
    {
        report.message.name = gAgentAvatarp->getFullname();
    }
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
        report.sessionLog = mSessionLogRow.check->get();
        report.previousLog = mPreviousLogRow.check->get() && !mPreviousLogFile.empty();
        report.systemInfo = mSystemInfoRow.check->get();
        report.settings = mSettingsRow.check->get();
    }

    gSavedSettings.setString("AlchemyFeedbackEmail", mRememberEmail->get() ? report.message.email : std::string());

    hideStatus();
    // Closing while it goes keeps the message, should it not arrive.
    saveDraft();
    ALFeedback::send(std::move(report),
                     [handle = getDerivedHandle<ALFloaterFeedback>()](const ALFeedback::Result& result)
                     { onResult(handle, result); });
    updateControls();
}

// static
void ALFloaterFeedback::onResult(LLHandle<ALFloaterFeedback> handle, const ALFeedback::Result& result)
{
    ALFloaterFeedback* self = handle.get();
    if (result.outcome == ALFeedback::Outcome::Sent)
    {
        sHeldUntil = LLFrameTimer::getTotalSeconds() + SEND_HOLD_SECONDS;
        clearDraft();
        notify_sent(result.eventId);
        if (self)
        {
            self->mSent = true;
            self->closeFloater();
        }
        return;
    }

    if (self && self->getVisible() && !self->isMinimized())
    {
        self->updateControls();
        self->showFailure(result);
    }
    else
    {
        if (self)
        {
            self->updateControls();
        }
        notify_failed(result);
    }
}

void ALFloaterFeedback::showFailure(const ALFeedback::Result& result)
{
    const std::string reason = failure_reason(result);
    switch (result.outcome)
    {
        case ALFeedback::Outcome::TooLarge:
            showStatus(reason, getString("action_without_attachments"), [this]() { send(false); });
            break;
        case ALFeedback::Outcome::Rejected:
            showStatus(reason, std::string(), nullptr);
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
    mPrivacyText->setVisible(false);
    mStatusPanel->setVisible(true);
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
    if (!path.empty())
    {
        viewText(title_string, ALFeedback::readLogTail(path));
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
    const std::string contents = LLFile::getContents(draft_file());
    LLSD draft;
    if (contents.empty() || !LlsdFromJsonString(contents, draft) || !draft.isMap())
    {
        return;
    }
    const std::string message = draft["message"].asString();
    if (trimmed(message).empty())
    {
        return;
    }

    mMessage->setText(message);
    mKind->setSelectedByValue(draft["kind"], true);
    // A report the key tied to a crash keeps that; otherwise the draft's.
    if (mLinked.empty() && draft.has("linked"))
    {
        mAssociatedEventId = draft["associated_event_id"].asString();
        mLinked = draft["linked"].asString();
        mLinkedAt = draft["linked_at"].asString();
    }
    if (draft["associated_event_id"].asString() == mAssociatedEventId)
    {
        mEventId = draft["event_id"].asString();
    }

    showStatus(getString("restored"), getString("action_discard"),
               [this]()
               {
                   mMessage->setText(LLStringUtil::null);
                   mEventId.clear();
                   clearDraft();
                   hideStatus();
                   updateCounter();
                   updateControls();
               });
}

void ALFloaterFeedback::saveDraft()
{
    const std::string message = mMessage->getText();
    if (trimmed(message).empty())
    {
        clearDraft();
        return;
    }

    LLSD draft;
    draft["kind"] = mKind->getValue();
    draft["message"] = message;
    draft["event_id"] = mEventId;
    draft["associated_event_id"] = mAssociatedEventId;
    if (!mLinked.empty())
    {
        draft["linked"] = mLinked;
        draft["linked_at"] = mLinkedAt;
    }
    const std::string json = LlsdToJson(draft);
    const std::string path = draft_file();
    std::error_code ec;
    LLFile file(path, LLFile::out | LLFile::trunc | LLFile::binary, ec);
    if (ec || file.write(json.data(), static_cast<S64>(json.size()), ec) != static_cast<S64>(json.size()))
    {
        LL_WARNS("Feedback") << "The draft could not be kept in " << path << ": " << ec.message() << LL_ENDL;
    }
}

// static
void ALFloaterFeedback::clearDraft()
{
    LLFile::remove(draft_file(), ENOENT);
}

ALFloaterFeedbackPreview::ALFloaterFeedbackPreview(const LLSD& key)
:   LLFloater(key)
{
}

bool ALFloaterFeedbackPreview::postBuild()
{
    mText = getChild<ALTextView>("text");
    return true;
}

void ALFloaterFeedbackPreview::draw()
{
    LLFloater::draw();

    if (mImage && !isMinimized())
    {
        // The picture, whole, as large as the floater lets it be.
        constexpr S32 MARGIN = 4;
        const LLRect rect = getLocalRect();
        const S32 room_width = rect.getWidth() - 2 * MARGIN;
        const S32 room_height = rect.getHeight() - getHeaderHeight() - 2 * MARGIN;
        if (room_width <= 0 || room_height <= 0)
        {
            return;
        }
        const F32 scale = std::min(static_cast<F32>(room_width) / mImageWidth,
                                   static_cast<F32>(room_height) / mImageHeight);
        const S32 width = ll_round(mImageWidth * scale);
        const S32 height = ll_round(mImageHeight * scale);
        const S32 x = MARGIN + (room_width - width) / 2;
        const S32 y = MARGIN + (room_height - height) / 2;
        gl_draw_scaled_image(x, y, width, height, mImage);
    }
}

// static
void ALFloaterFeedbackPreview::showText(const std::string& title, std::string_view text)
{
    ALFloaterFeedbackPreview* floater = LLFloaterReg::showTypedInstance<ALFloaterFeedbackPreview>("feedback_preview",
                                                                                               LLSD(), true);
    if (!floater)
    {
        return;
    }
    floater->setTitle(title);
    floater->mImage = nullptr;
    floater->mText->setVisible(true);
    floater->mText->setText(text);
}

// static
void ALFloaterFeedbackPreview::showImage(const std::string& title, const LLPointer<LLImageRaw>& image)
{
    ALFloaterFeedbackPreview* floater = LLFloaterReg::showTypedInstance<ALFloaterFeedbackPreview>("feedback_preview",
                                                                                               LLSD(), true);
    if (!floater || image.isNull())
    {
        return;
    }
    floater->setTitle(title);
    floater->mText->setVisible(false);
    floater->mImage = LLViewerTextureManager::getLocalTexture(image.get(), false);
    floater->mImageWidth = image->getWidth();
    floater->mImageHeight = image->getHeight();
}
