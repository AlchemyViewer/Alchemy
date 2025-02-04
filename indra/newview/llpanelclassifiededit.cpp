/**
 * @file llpanelclassifiededit.cpp
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

#include "llviewerprecompiledheaders.h"
#include "llpanelclassifiededit.h"

#include "llagent.h"
#include "llclassifiedflags.h"
#include "llclassifiedinfo.h"
#include "llcombobox.h"
#include "llfloaterreg.h"
#include "lllineeditor.h"
#include "lllogininstance.h"
#include "llparcel.h"
#include "llstatusbar.h"
#include "lltexteditor.h"
#include "lltexturectrl.h"
#include "lltrans.h"
#include "llnotificationsutil.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"

static const S32 CB_ITEM_MATURE = 0;
static const S32 CB_ITEM_PG     = 1;

LLPanelClassifiedEdit::LLPanelClassifiedEdit() :
    LLPanelClassifiedInfo(),
    mIsNew(false),
    mIsNewWithErrors(false),
    mCanClose(false),
    mPublishFloater(nullptr)
{
}

LLPanelClassifiedEdit::~LLPanelClassifiedEdit() {}

// static
LLPanelClassifiedEdit* LLPanelClassifiedEdit::create()
{
    LLPanelClassifiedEdit* panel = new LLPanelClassifiedEdit();
    panel->buildFromFile("panel_edit_classified.xml");
    return panel;
}

BOOL LLPanelClassifiedEdit::postBuild()
{
    LLPanelClassifiedInfo::postBuild();

    LLUICtrl* edit_icon = getChild<LLUICtrl>("edit_icon");
    mSnapshotCtrl->setMouseEnterCallback(boost::bind(&LLPanelClassifiedEdit::onTexturePickerMouseEnter, this, edit_icon));
    mSnapshotCtrl->setMouseLeaveCallback(boost::bind(&LLPanelClassifiedEdit::onTexturePickerMouseLeave, this, edit_icon));
    edit_icon->setVisible(false);

    getChild<LLLineEditor>("classified_name")->setKeystrokeCallback(boost::bind(&LLPanelClassifiedEdit::onChange, this), nullptr);
    getChild<LLTextEditor>("classified_desc")->setKeystrokeCallback(boost::bind(&LLPanelClassifiedEdit::onChange, this));

    LLComboBox* combobox = getChild<LLComboBox>("category");
    for (auto const& category : LLClassifiedInfo::sCategories)
    {
        combobox->add(LLTrans::getString(category.second));
    }
    combobox->setCommitCallback(boost::bind(&LLPanelClassifiedEdit::onChange, this));

    childSetCommitCallback("content_type", boost::bind(&LLPanelClassifiedEdit::onChange, this), nullptr);
    childSetCommitCallback("price_for_listing", boost::bind(&LLPanelClassifiedEdit::onChange, this), nullptr);
    childSetCommitCallback("auto_renew", boost::bind(&LLPanelClassifiedEdit::onChange, this), nullptr);

    childSetAction("save_changes_btn", boost::bind(&LLPanelClassifiedEdit::onSaveClick, this));
    childSetAction("set_to_curr_location_btn", boost::bind(&LLPanelClassifiedEdit::onSetLocationClick, this));

    mSnapshotCtrl->setOnSelectCallback(boost::bind(&LLPanelClassifiedEdit::onTextureSelected, this));

    return TRUE;
}

void LLPanelClassifiedEdit::fillIn(const LLSD& key)
{
    setAvatarId(gAgent.getID());

    if (key.isUndefined())
    {
        setPosGlobal(gAgent.getPositionGlobal());

        LLUUID snapshot_id = LLUUID::null;
        std::string desc;
        LLParcel* parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();

        if (parcel)
        {
            desc = parcel->getDesc();
            snapshot_id = parcel->getSnapshotID();
        }

        std::string region_name = LLTrans::getString("ClassifiedUpdateAfterPublish");
        LLViewerRegion* region = gAgent.getRegion();
        if (region)
        {
            region_name = region->getName();
        }

        getChild<LLUICtrl>("classified_name")->setValue(makeClassifiedName());
        getChild<LLUICtrl>("classified_desc")->setValue(desc);
        setSnapshotId(snapshot_id);
        setClassifiedLocation(createLocationText(getString("location_notice"), region_name, getPosGlobal()));
        // server will set valid parcel id
        setParcelId(LLUUID::null);
    }
    else
    {
        setClassifiedId(key["classified_id"]);
        setClassifiedName(key["name"]);
        setDescription(key["desc"]);
        setSnapshotId(key["snapshot_id"]);
        setCategory((U32) key["category"].asInteger());
        setContentType((U32) key["content_type"].asInteger());
        setClassifiedLocation(key["location_text"]);
        getChild<LLUICtrl>("auto_renew")->setValue(key["auto_renew"]);
        getChild<LLUICtrl>("price_for_listing")->setValue(key["price_for_listing"].asInteger());
    }
}

void LLPanelClassifiedEdit::onOpen(const LLSD& key)
{
    mIsNew = key.isUndefined();

    scrollToTop();

    // classified is not created yet
    bool is_new = isNew() || isNewWithErrors();

    if (is_new)
    {
        resetData();
        resetControls();

        fillIn(key);

        if (isNew())
        {
            LLAvatarPropertiesProcessor::getInstance()->addObserver(getAvatarId(), this);
        }
    }
    else
    {
        LLPanelClassifiedInfo::onOpen(key);
    }

    std::string save_btn_label = is_new ? getString("publish_label") : getString("save_label");
    getChild<LLUICtrl>("save_changes_btn")->setLabelArg("[LABEL]", save_btn_label);

    enableVerbs(is_new);
    enableEditing(is_new);
    showEditing(!is_new);
    resetDirty();
    setInfoLoaded(false);
}

void LLPanelClassifiedEdit::processProperties(void* data, EAvatarProcessorType type)
{
    if (APT_CLASSIFIED_INFO == type)
    {
        LLAvatarClassifiedInfo* c_info = static_cast<LLAvatarClassifiedInfo*>(data);
        if (c_info && getClassifiedId() == c_info->classified_id)
        {
            // see LLPanelClassifiedEdit::sendUpdate() for notes
            mIsNewWithErrors = false;
            // for just created classified - panel will probably be closed when we get here.
            if (!getVisible())
            {
                return;
            }

            enableEditing(true);

            setClassifiedName(c_info->name);
            setDescription(c_info->description);
            setSnapshotId(c_info->snapshot_id);
            setParcelId(c_info->parcel_id);
            setPosGlobal(c_info->pos_global);

            setClassifiedLocation(createLocationText(c_info->parcel_name, c_info->sim_name, c_info->pos_global));
            // *HACK see LLPanelClassifiedEdit::sendUpdate()
            setCategory(c_info->category - 1);

            bool mature     = is_cf_mature(c_info->flags);
            bool auto_renew = is_cf_auto_renew(c_info->flags);

            setContentType(mature ? CB_ITEM_MATURE : CB_ITEM_PG);
            getChild<LLUICtrl>("auto_renew")->setValue(auto_renew);
            getChild<LLUICtrl>("price_for_listing")->setValue(c_info->price_for_listing);
            getChildView("price_for_listing")->setEnabled(isNew());

            resetDirty();
            setInfoLoaded(true);
            enableVerbs(false);

            // for just created classified - in case user opened edit panel before processProperties() callback
            getChild<LLUICtrl>("save_changes_btn")->setLabelArg("[LABEL]", getString("save_label"));
        }
    }
}

BOOL LLPanelClassifiedEdit::isDirty() const
{
    if (mIsNew)
    {
        return TRUE;
    }

    BOOL dirty = false;

    dirty |= LLPanelClassifiedInfo::isDirty();
    dirty |= getChild<LLUICtrl>("classified_snapshot")->isDirty();
    dirty |= getChild<LLUICtrl>("classified_name")->isDirty();
    dirty |= getChild<LLUICtrl>("classified_desc")->isDirty();
    dirty |= getChild<LLUICtrl>("category")->isDirty();
    dirty |= getChild<LLUICtrl>("content_type")->isDirty();
    dirty |= getChild<LLUICtrl>("auto_renew")->isDirty();
    dirty |= getChild<LLUICtrl>("price_for_listing")->isDirty();

    return dirty;
}

void LLPanelClassifiedEdit::resetDirty()
{
    LLPanelClassifiedInfo::resetDirty();
    getChild<LLUICtrl>("classified_snapshot")->resetDirty();
    getChild<LLUICtrl>("classified_name")->resetDirty();

    LLTextEditor* desc = getChild<LLTextEditor>("classified_desc");
    // call blockUndo() to really reset dirty(and make isDirty work as intended)
    desc->blockUndo();
    desc->resetDirty();

    getChild<LLUICtrl>("category")->resetDirty();
    getChild<LLUICtrl>("content_type")->resetDirty();
    getChild<LLUICtrl>("auto_renew")->resetDirty();
    getChild<LLUICtrl>("price_for_listing")->resetDirty();
}

void LLPanelClassifiedEdit::setSaveCallback(const commit_signal_t::slot_type& cb) { mSaveButtonClickedSignal.connect(cb); }

void LLPanelClassifiedEdit::setCancelCallback(const commit_signal_t::slot_type& cb)
{
    getChild<LLButton>("cancel_btn")->setClickedCallback(cb);
}

void LLPanelClassifiedEdit::resetControls()
{
    LLPanelClassifiedInfo::resetControls();

    getChild<LLComboBox>("category")->setCurrentByIndex(0);
    getChild<LLComboBox>("content_type")->setCurrentByIndex(0);
    getChild<LLUICtrl>("auto_renew")->setValue(false);
    if (LLLoginInstance::getInstance()->hasResponse("classified_fee"))
    {
        getChild<LLUICtrl>("price_for_listing")->setValue(LLLoginInstance::getInstance()->getResponse("classified_fee").asInteger());
    }
    else
    {
        getChild<LLUICtrl>("price_for_listing")->setValue(MINIMUM_PRICE_FOR_LISTING);
    }
    getChildView("price_for_listing")->setEnabled(TRUE);
}

bool LLPanelClassifiedEdit::canClose() { return mCanClose; }

void LLPanelClassifiedEdit::draw()
{
    LLPanel::draw();

    // Need to re-stretch on every draw because LLTextureCtrl::onSelectCallback
    // does not trigger callbacks when user navigates through images.
    stretchSnapshot();
}

void LLPanelClassifiedEdit::stretchSnapshot()
{
    LLPanelClassifiedInfo::stretchSnapshot();

    getChild<LLUICtrl>("edit_icon")->setShape(mSnapshotCtrl->getRect());
}

U32 LLPanelClassifiedEdit::getContentType()
{
    LLComboBox* ct_cb = getChild<LLComboBox>("content_type");
    return ct_cb->getCurrentIndex();
}

void LLPanelClassifiedEdit::setContentType(U32 content_type)
{
    LLComboBox* ct_cb = getChild<LLComboBox>("content_type");
    ct_cb->setCurrentByIndex(content_type);
    ct_cb->resetDirty();
}

bool LLPanelClassifiedEdit::getAutoRenew() { return getChild<LLUICtrl>("auto_renew")->getValue().asBoolean(); }

void LLPanelClassifiedEdit::sendUpdate()
{
    LLAvatarClassifiedInfo c_data;

    if (getClassifiedId().isNull())
    {
        setClassifiedId(LLUUID::generateNewID());
    }

    c_data.agent_id      = gAgent.getID();
    c_data.classified_id = getClassifiedId();
    // *HACK
    // Categories on server start with 1 while combo-box index starts with 0
    c_data.category          = getCategory() + 1;
    c_data.name              = getClassifiedName();
    c_data.description       = getDescription();
    c_data.parcel_id         = getParcelId();
    c_data.snapshot_id       = getSnapshotId();
    c_data.pos_global        = getPosGlobal();
    c_data.flags             = getFlags();
    c_data.price_for_listing = getPriceForListing();

    LLAvatarPropertiesProcessor::getInstance()->sendClassifiedInfoUpdate(&c_data);

    if (isNew())
    {
        // Lets assume there will be some error.
        // Successful sendClassifiedInfoUpdate will trigger processProperties and
        // let us know there was no error.
        mIsNewWithErrors = true;
    }
}

U32 LLPanelClassifiedEdit::getCategory()
{
    LLComboBox* cat_cb = getChild<LLComboBox>("category");
    return cat_cb->getCurrentIndex();
}

void LLPanelClassifiedEdit::setCategory(U32 category)
{
    LLComboBox* cat_cb = getChild<LLComboBox>("category");
    cat_cb->setCurrentByIndex(category);
    cat_cb->resetDirty();
}

U8 LLPanelClassifiedEdit::getFlags()
{
    bool auto_renew = getChild<LLUICtrl>("auto_renew")->getValue().asBoolean();

    LLComboBox* content_cb = getChild<LLComboBox>("content_type");
    bool mature = content_cb->getCurrentIndex() == CB_ITEM_MATURE;

    return pack_classified_flags_request(auto_renew, false, mature, false);
}

void LLPanelClassifiedEdit::enableVerbs(bool enable) { getChildView("save_changes_btn")->setEnabled(enable); }

void LLPanelClassifiedEdit::enableEditing(bool enable)
{
    getChildView("classified_snapshot")->setEnabled(enable);
    getChildView("classified_name")->setEnabled(enable);
    getChildView("classified_desc")->setEnabled(enable);
    getChildView("set_to_curr_location_btn")->setEnabled(enable);
    getChildView("category")->setEnabled(enable);
    getChildView("content_type")->setEnabled(enable);
    getChildView("price_for_listing")->setEnabled(enable);
    getChildView("auto_renew")->setEnabled(enable);
}

void LLPanelClassifiedEdit::showEditing(bool show)
{
    getChildView("price_for_listing_label")->setVisible(show);
    getChildView("price_for_listing")->setVisible(show);
}

std::string LLPanelClassifiedEdit::makeClassifiedName()
{
    std::string name;

    LLParcel* parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();
    if (parcel)
    {
        name = parcel->getName();
    }

    if (!name.empty())
    {
        return name;
    }

    LLViewerRegion* region = gAgent.getRegion();
    if (region)
    {
        name = region->getName();
    }

    return name;
}

S32 LLPanelClassifiedEdit::getPriceForListing() { return getChild<LLUICtrl>("price_for_listing")->getValue().asInteger(); }

void LLPanelClassifiedEdit::setPriceForListing(S32 price) { getChild<LLUICtrl>("price_for_listing")->setValue(price); }

void LLPanelClassifiedEdit::onSetLocationClick()
{
    setPosGlobal(gAgent.getPositionGlobal());
    setParcelId(LLUUID::null);

    std::string region_name = LLTrans::getString("ClassifiedUpdateAfterPublish");
    LLViewerRegion* region = gAgent.getRegion();
    if (region)
    {
        region_name = region->getName();
    }

    setClassifiedLocation(createLocationText(getString("location_notice"), region_name, getPosGlobal()));

    // mark classified as dirty
    setValue(LLSD());

    onChange();
}

void LLPanelClassifiedEdit::onChange() { enableVerbs(isDirty()); }

void LLPanelClassifiedEdit::onSaveClick()
{
    mCanClose = false;

    if (!isValidName())
    {
        notifyInvalidName();
        return;
    }
    if (isNew() || isNewWithErrors())
    {
        if (gStatusBar->getBalance() < getPriceForListing())
        {
            LLNotificationsUtil::add("ClassifiedInsufficientFunds");
            return;
        }

        mPublishFloater = LLFloaterReg::findTypedInstance<LLFloaterPublishClassified>("publish_classified", LLSD());

        if (!mPublishFloater)
        {
            mPublishFloater = LLFloaterReg::getTypedInstance<LLFloaterPublishClassified>("publish_classified", LLSD());

            mPublishFloater->setPublishClickedCallback(boost::bind(&LLPanelClassifiedEdit::onPublishFloaterPublishClicked, this));
        }

        // set spinner value before it has focus or value wont be set
        mPublishFloater->setPrice(getPriceForListing());
        mPublishFloater->openFloater(mPublishFloater->getKey());
        mPublishFloater->center();
    }
    else
    {
        doSave();
    }
}

void LLPanelClassifiedEdit::doSave()
{
    mCanClose = true;
    sendUpdate();
    resetDirty();

    mSaveButtonClickedSignal(this, LLSD());
}

void LLPanelClassifiedEdit::onPublishFloaterPublishClicked()
{
    setPriceForListing(mPublishFloater->getPrice());

    doSave();
}

bool LLPanelClassifiedEdit::isValidName()
{
    const std::string& name = getClassifiedName();
    if (name.empty())
    {
        return false;
    }
    return (isalnum(name[0]));
}

void LLPanelClassifiedEdit::notifyInvalidName()
{
    const std::string& name = getClassifiedName();
    if (name.empty())
    {
        LLNotificationsUtil::add("BlankClassifiedName");
    }
    else if (!isalnum(name[0]))
    {
        LLNotificationsUtil::add("ClassifiedMustBeAlphanumeric");
    }
}

void LLPanelClassifiedEdit::onTexturePickerMouseEnter(LLUICtrl* ctrl) { ctrl->setVisible(TRUE); }

void LLPanelClassifiedEdit::onTexturePickerMouseLeave(LLUICtrl* ctrl) { ctrl->setVisible(FALSE); }

void LLPanelClassifiedEdit::onTextureSelected()
{
    setSnapshotId(mSnapshotCtrl->getValue().asUUID());
    onChange();
}
