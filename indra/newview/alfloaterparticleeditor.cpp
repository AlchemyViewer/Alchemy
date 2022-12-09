/**
 * @file alfloaterparticleeditor.cpp
 * @brief Particle Editor
 *
 * Copyright (C) 2011, Zi Ree @ Second Life
 * Copyright (C) 2015, Cinder Roxley <cinder@sdf.org>
 * Copyright (C) 2020, Rye Mutt <rye@alchemyviewer.org>
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
 */

#include "llviewerprecompiledheaders.h"
#include "alfloaterparticleeditor.h"

#include "llagent.h"
#include "llappviewer.h"
#include "llcheckboxctrl.h"
#include "llclipboard.h"
#include "llcolorswatch.h"
#include "llcombobox.h"
#include "llfoldertype.h"
#include "llinventorymodel.h"
#include "llinventorytype.h"
#include "llnotificationsutil.h"
#include "llpermissions.h"
#include "llpreviewscript.h"
#include "llsd.h"
#include "lltexturectrl.h"
#include "lltoolmgr.h"
#include "lltoolobjpicker.h"
#include "llviewerassetupload.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerpartsim.h"
#include "llviewerpartsource.h"
#include "llviewerregion.h"
#include "llviewertexture.h"
#include "llwindow.h"

struct lsl_part_st {
	U8 flag;
	const char* script_const;
	lsl_part_st(const U8 f, const char* sc)
	{
		flag = f, script_const = sc;
	}
};

static const std::map<std::string, lsl_part_st> sParticlePatterns{
	{ "drop", lsl_part_st(LLPartSysData::LL_PART_SRC_PATTERN_DROP, "PSYS_SRC_PATTERN_DROP") },
	{ "explode", lsl_part_st(LLPartSysData::LL_PART_SRC_PATTERN_EXPLODE, "PSYS_SRC_PATTERN_EXPLODE") },
	{ "angle", lsl_part_st(LLPartSysData::LL_PART_SRC_PATTERN_ANGLE, "PSYS_SRC_PATTERN_ANGLE") },
	{ "angle_cone", lsl_part_st(LLPartSysData::LL_PART_SRC_PATTERN_ANGLE_CONE, "PSYS_SRC_PATTERN_ANGLE_CONE") },
	{ "angle_cone_empty", lsl_part_st(LLPartSysData::LL_PART_SRC_PATTERN_ANGLE_CONE_EMPTY, "PSYS_SRC_PATTERN_ANGLE_CONE_EMPTY") }
};

static const std::map<std::string, lsl_part_st> sParticleBlends{
	{ "blend_one", lsl_part_st(LLPartData::LL_PART_BF_ONE, "PSYS_PART_BF_ONE") },
	{ "blend_zero", lsl_part_st(LLPartData::LL_PART_BF_ZERO, "PSYS_PART_BF_ZERO") },
	{ "blend_dest_color", lsl_part_st(LLPartData::LL_PART_BF_DEST_COLOR, "PSYS_PART_BF_DEST_COLOR") },
	{ "blend_src_color", lsl_part_st(LLPartData::LL_PART_BF_SOURCE_COLOR, "PSYS_PART_BF_SOURCE_COLOR") },
	{ "blend_one_minus_dest_color", lsl_part_st(LLPartData::LL_PART_BF_ONE_MINUS_DEST_COLOR, "PSYS_PART_BF_ONE_MINUS_DEST_COLOR") },
	{ "blend_one_minus_src_color", lsl_part_st(LLPartData::LL_PART_BF_ONE_MINUS_SOURCE_COLOR, "PSYS_PART_BF_SOURCE_ALPHA") },
	{ "blend_src_alpha", lsl_part_st(LLPartData::LL_PART_BF_SOURCE_ALPHA, "PSYS_PART_BF_SOURCE_ALPHA") },
	{ "blend_one_minus_src_alpha", lsl_part_st(LLPartData::LL_PART_BF_ONE_MINUS_SOURCE_ALPHA, "PSYS_PART_BF_ONE_MINUS_SOURCE_ALPHA") }
};

static const std::string PARTICLE_SCRIPT_NAME = "New Particle Script";

ALFloaterParticleEditor::ALFloaterParticleEditor(const LLSD& key)
	: LLFloater(key), mChanged(false), mCloseAfterSave(false), mObject(nullptr), mTexture(nullptr)
	, mParticleScriptInventoryItem(nullptr), mPatternTypeCombo(nullptr), mTexturePicker(nullptr)
	, mBurstRateCtrl(nullptr), mBurstCountCtrl(nullptr), mBurstRadiusCtrl(nullptr)
	, mAngleBeginCtrl(nullptr), mAngleEndCtrl(nullptr), mBurstSpeedMinCtrl(nullptr)
	, mBurstSpeedMaxCtrl(nullptr), mStartAlphaCtrl(nullptr), mEndAlphaCtrl(nullptr)
	, mScaleStartXCtrl(nullptr), mScaleStartYCtrl(nullptr), mScaleEndXCtrl(nullptr)
	, mScaleEndYCtrl(nullptr), mSourceMaxAgeCtrl(nullptr), mParticlesMaxAgeCtrl(nullptr)
	, mStartGlowCtrl(nullptr), mEndGlowCtrl(nullptr), mAcellerationXCtrl(nullptr)
	, mAcellerationYCtrl(nullptr), mAcellerationZCtrl(nullptr), mOmegaXCtrl(nullptr)
	, mOmegaYCtrl(nullptr), mOmegaZCtrl(nullptr), mBlendFuncSrcCombo(nullptr)
	, mBlendFuncDestCombo(nullptr), mBounceCheckBox(nullptr), mEmissiveCheckBox(nullptr)
	, mFollowSourceCheckBox(nullptr), mFollowVelocityCheckBox(nullptr), mInterpolateColorCheckBox(nullptr)
	, mInterpolateScaleCheckBox(nullptr), mTargetPositionCheckBox(nullptr), mTargetLinearCheckBox(nullptr)
	, mWindCheckBox(nullptr), mRibbonCheckBox(nullptr), mTargetKeyInput(nullptr)
	, mClearTargetButton(nullptr), mPickTargetButton(nullptr), mCopyToLSLButton(nullptr)
	, mInjectScriptButton(nullptr), mStartColorSelector(nullptr), mEndColorSelector(nullptr)
{
	mCommitCallbackRegistrar.add("Particle.Edit", [this](LLUICtrl* ctrl, const LLSD& param) { onParameterChange(); });
	mDefaultParticleTexture = LLViewerFetchedTexture::sPixieSmallImagep;
}

ALFloaterParticleEditor::~ALFloaterParticleEditor()
{
	clearParticles();
}

BOOL ALFloaterParticleEditor::postBuild()
{
	LLPanel* panel = getChild<LLPanel>("burst_panel");
	mBurstRateCtrl = panel->getChild<LLUICtrl>("burst_rate");
	mBurstCountCtrl = panel->getChild<LLUICtrl>("burst_count");
	mBurstRadiusCtrl = panel->getChild<LLUICtrl>("burst_radius");
	mBurstSpeedMinCtrl = panel->getChild<LLUICtrl>("burst_speed_min");
	mBurstSpeedMaxCtrl = panel->getChild<LLUICtrl>("burst_speed_max");
	mSourceMaxAgeCtrl = panel->getChild<LLUICtrl>("source_max_age");
	mParticlesMaxAgeCtrl = panel->getChild<LLUICtrl>("particle_max_age");

	panel = getChild<LLPanel>("angle_panel");
	mAngleBeginCtrl = panel->getChild<LLUICtrl>("angle_begin");
	mAngleEndCtrl = panel->getChild<LLUICtrl>("angle_end");
	mScaleStartXCtrl = panel->getChild<LLUICtrl>("scale_start_x");
	mScaleStartYCtrl = panel->getChild<LLUICtrl>("scale_start_y");
	mScaleEndXCtrl = panel->getChild<LLUICtrl>("scale_end_x");
	mScaleEndYCtrl = panel->getChild<LLUICtrl>("scale_end_y");

	panel = getChild<LLPanel>("alpha_panel");
	mStartAlphaCtrl = panel->getChild<LLUICtrl>("start_alpha");
	mEndAlphaCtrl = panel->getChild<LLUICtrl>("end_alpha");
	mStartGlowCtrl = panel->getChild<LLUICtrl>("start_glow");
	mEndGlowCtrl = panel->getChild<LLUICtrl>("end_glow");

	panel = getChild<LLPanel>("omega_panel");
	mAcellerationXCtrl = panel->getChild<LLUICtrl>("acceleration_x");
	mAcellerationYCtrl = panel->getChild<LLUICtrl>("acceleration_y");
	mAcellerationZCtrl = panel->getChild<LLUICtrl>("acceleration_z");
	mOmegaXCtrl = panel->getChild<LLUICtrl>("omega_x");
	mOmegaYCtrl = panel->getChild<LLUICtrl>("omega_y");
	mOmegaZCtrl = panel->getChild<LLUICtrl>("omega_z");

	panel = getChild<LLPanel>("color_panel");
	mStartColorSelector = panel->getChild<LLColorSwatchCtrl>("start_color_selector");
	mEndColorSelector = panel->getChild<LLColorSwatchCtrl>("end_color_selector");
	mTexturePicker = panel->getChild<LLTextureCtrl>("texture_picker");
	mBlendFuncSrcCombo = panel->getChild<LLComboBox>("blend_func_src_combo");
	mBlendFuncDestCombo = panel->getChild<LLComboBox>("blend_func_dest_combo");

	panel = getChild<LLPanel>("checkbox_panel");
	mPatternTypeCombo = panel->getChild<LLComboBox>("pattern_type_combo");
	mBounceCheckBox = panel->getChild<LLCheckBoxCtrl>("bounce_checkbox");
	mEmissiveCheckBox = panel->getChild<LLCheckBoxCtrl>("emissive_checkbox");
	mFollowSourceCheckBox = panel->getChild<LLCheckBoxCtrl>("follow_source_checkbox");
	mFollowVelocityCheckBox = panel->getChild<LLCheckBoxCtrl>("follow_velocity_checkbox");
	mInterpolateColorCheckBox = panel->getChild<LLCheckBoxCtrl>("interpolate_color_checkbox");
	mInterpolateScaleCheckBox = panel->getChild<LLCheckBoxCtrl>("interpolate_scale_checkbox");
	mTargetPositionCheckBox = panel->getChild<LLCheckBoxCtrl>("target_position_checkbox");
	mTargetLinearCheckBox = panel->getChild<LLCheckBoxCtrl>("target_linear_checkbox");
	mWindCheckBox = panel->getChild<LLCheckBoxCtrl>("wind_checkbox");
	mRibbonCheckBox = panel->getChild<LLCheckBoxCtrl>("ribbon_checkbox");

	mTargetKeyInput = panel->getChild<LLUICtrl>("target_key_input");
	mClearTargetButton = panel->getChild<LLButton>("clear_target_button");
	mPickTargetButton = panel->getChild<LLButton>("pick_target_button");
	mInjectScriptButton = panel->getChild<LLButton>("inject_button");
	mCopyToLSLButton = panel->getChild<LLButton>("copy_button");

	mClearTargetButton->setCommitCallback([this](LLUICtrl* ctrl, const LLSD& param) { onClickClearTarget(); });
	mPickTargetButton->setCommitCallback([this](LLUICtrl* ctrl, const LLSD& param) { onClickTargetPicker(); });
	mInjectScriptButton->setCommitCallback([this](LLUICtrl* ctrl, const LLSD& param) { injectScript(); });
	mCopyToLSLButton->setCommitCallback([this](LLUICtrl* ctrl, const LLSD& param) { onClickCopy(); });

	mBlendFuncSrcCombo->setValue("blend_src_alpha");
	mBlendFuncDestCombo->setValue("blend_one_minus_src_alpha");

	onParameterChange();

	return TRUE;
}

BOOL ALFloaterParticleEditor::canClose()
{
    if (!hasChanged())
    {
        return TRUE;
    }
    else
    {
        // Bring up view-modal dialog: Save changes? Yes, No, Cancel
        LLNotificationsUtil::add("ParticleSaveChanges", LLSD(), LLSD(), boost::bind(&ALFloaterParticleEditor::handleSaveDialog, this, _1, _2));
        return FALSE;
    }
}

bool ALFloaterParticleEditor::handleSaveDialog(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    switch (option)
    {
    case 0:  // "Yes"
             // close after saving
        mCloseAfterSave = true;
        injectScript();
        break;

    case 1:  // "No"
		mChanged = false;
        closeFloater();
        break;

    case 2: // "Cancel"
    default:
        break;
    }
    return false;
}

void ALFloaterParticleEditor::clearParticles()
{
	if (!mObject)
		return;

	LL_DEBUGS("ParticleEditor") << "clearing particles from " << mObject->getID() << LL_ENDL;

	LLViewerPartSim::getInstance()->clearParticlesByOwnerID(mObject->getID());
}

void ALFloaterParticleEditor::updateParticles()
{
	if (!mObject)
		return;

	clearParticles();
	LLPointer<LLViewerPartSourceScript> pss = LLViewerPartSourceScript::createPSS(mObject, mParticles);

	pss->setOwnerUUID(mObject->getID());
	pss->setImage(mTexture);

	LLViewerPartSim::getInstance()->addPartSource(pss);
}

void ALFloaterParticleEditor::setObject(LLViewerObject* objectp)
{
	if (objectp)
	{
		mObject = objectp;

		LL_DEBUGS("ParticleEditor") << "adding particles to " << mObject->getID() << LL_ENDL;

		updateParticles();
	}
}

void ALFloaterParticleEditor::onParameterChange()
{
    mChanged = true;
	mParticles.mPattern = sParticlePatterns.at(mPatternTypeCombo->getSelectedValue()).flag;
	mParticles.mPartImageID = mTexturePicker->getImageAssetID();

	// remember the selected texture here to give updateParticles() a UUID to work with
	mTexture = LLViewerTextureManager::getFetchedTexture(mTexturePicker->getImageAssetID());

	if (mTexture->getID() == IMG_DEFAULT || mTexture->getID().isNull())
	{
		mTexture = mDefaultParticleTexture;
	}

	// limit burst rate to 0.01 to avoid internal freeze, script still gets the real value
	mParticles.mBurstRate = llmax<float>(0.01f, mBurstRateCtrl->getValue().asReal());
	mParticles.mBurstPartCount = mBurstCountCtrl->getValue().asInteger();
	mParticles.mBurstRadius = mBurstRadiusCtrl->getValue().asReal();
	mParticles.mInnerAngle = mAngleBeginCtrl->getValue().asReal();
	mParticles.mOuterAngle = mAngleEndCtrl->getValue().asReal();
	mParticles.mBurstSpeedMin = mBurstSpeedMinCtrl->getValue().asReal();
	mParticles.mBurstSpeedMax = mBurstSpeedMaxCtrl->getValue().asReal();
	mParticles.mPartData.setStartAlpha(mStartAlphaCtrl->getValue().asReal());
	mParticles.mPartData.setEndAlpha(mEndAlphaCtrl->getValue().asReal());
	mParticles.mPartData.setStartScale(mScaleStartXCtrl->getValue().asReal(),
					   mScaleStartYCtrl->getValue().asReal());
	mParticles.mPartData.setEndScale(mScaleEndXCtrl->getValue().asReal(),
					 mScaleEndYCtrl->getValue().asReal());
	mParticles.mMaxAge = mSourceMaxAgeCtrl->getValue().asReal();
	mParticles.mPartData.setMaxAge(mParticlesMaxAgeCtrl->getValue().asReal());

	mParticles.mPartData.mStartGlow = mStartGlowCtrl->getValue().asReal();
	mParticles.mPartData.mEndGlow = mEndGlowCtrl->getValue().asReal();

	mParticles.mPartData.mBlendFuncSource = sParticleBlends.at(mBlendFuncSrcCombo->getSelectedValue()).flag;
	mParticles.mPartData.mBlendFuncDest = sParticleBlends.at(mBlendFuncDestCombo->getSelectedValue()).flag;

	U32 flags = 0;
	if (mBounceCheckBox->getValue().asBoolean())			flags |= LLPartData::LL_PART_BOUNCE_MASK;
	if (mEmissiveCheckBox->getValue().asBoolean())			flags |= LLPartData::LL_PART_EMISSIVE_MASK;
	if (mFollowSourceCheckBox->getValue().asBoolean())		flags |= LLPartData::LL_PART_FOLLOW_SRC_MASK;
	if (mFollowVelocityCheckBox->getValue().asBoolean())	flags |= LLPartData::LL_PART_FOLLOW_VELOCITY_MASK;
	if (mInterpolateColorCheckBox->getValue().asBoolean())	flags |= LLPartData::LL_PART_INTERP_COLOR_MASK;
	if (mInterpolateScaleCheckBox->getValue().asBoolean())	flags |= LLPartData::LL_PART_INTERP_SCALE_MASK;
	if (mTargetPositionCheckBox->getValue().asBoolean())	flags |= LLPartData::LL_PART_TARGET_POS_MASK;
	if (mTargetLinearCheckBox->getValue().asBoolean())		flags |= LLPartData::LL_PART_TARGET_LINEAR_MASK;
	if (mWindCheckBox->getValue().asBoolean())				flags |= LLPartData::LL_PART_WIND_MASK;
	if (mRibbonCheckBox->getValue().asBoolean())			flags |= LLPartData::LL_PART_RIBBON_MASK;
	mParticles.mPartData.setFlags(flags);
	mParticles.setUseNewAngle();

	mParticles.mTargetUUID = mTargetKeyInput->getValue().asUUID();

	mParticles.mPartAccel = LLVector3(mAcellerationXCtrl->getValue().asReal(),
					  mAcellerationYCtrl->getValue().asReal(),
					  mAcellerationZCtrl->getValue().asReal());
	mParticles.mAngularVelocity = LLVector3(mOmegaXCtrl->getValue().asReal(),
						mOmegaYCtrl->getValue().asReal(),
						mOmegaZCtrl->getValue().asReal());

	LLColor4 color = mStartColorSelector->get();
	mParticles.mPartData.setStartColor(LLVector3(color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE]));
	color = mEndColorSelector->get();
	mParticles.mPartData.setEndColor(LLVector3(color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE]));

	updateUI();
	updateParticles();
}

void ALFloaterParticleEditor::updateUI()
{
	U8 pattern = sParticlePatterns.at(mPatternTypeCombo->getValue()).flag;
	BOOL drop_pattern = (pattern == LLPartSysData::LL_PART_SRC_PATTERN_DROP);
	BOOL explode_pattern = (pattern == LLPartSysData::LL_PART_SRC_PATTERN_EXPLODE);
	BOOL target_linear = mTargetLinearCheckBox->getValue();
	BOOL interpolate_color = mInterpolateColorCheckBox->getValue();
	BOOL interpolate_scale = mInterpolateScaleCheckBox->getValue();
	BOOL target_enabled = target_linear | (mTargetPositionCheckBox->getValue().asBoolean() ? TRUE : FALSE);

	mBurstRadiusCtrl->setEnabled(!(target_linear | (mFollowSourceCheckBox->getValue().asBoolean() ? TRUE : FALSE) | drop_pattern));
	mBurstSpeedMinCtrl->setEnabled(!(target_linear | drop_pattern));
	mBurstSpeedMaxCtrl->setEnabled(!(target_linear | drop_pattern));

	// disabling a color swatch does nothing visually, so we also set alpha
	LLColor4 end_color = mEndColorSelector->get();
	end_color.setAlpha(interpolate_color ? 1.0f : 0.0f);

	mEndAlphaCtrl->setEnabled(interpolate_color);
	mEndColorSelector->set(end_color);
	mEndColorSelector->setEnabled(interpolate_color);

	mScaleEndXCtrl->setEnabled(interpolate_scale);
	mScaleEndYCtrl->setEnabled(interpolate_scale);

	mTargetPositionCheckBox->setEnabled(!target_linear);
	mTargetKeyInput->setEnabled(target_enabled);
	mPickTargetButton->setEnabled(target_enabled);
	mClearTargetButton->setEnabled(target_enabled);

	mAcellerationXCtrl->setEnabled(!target_linear);
	mAcellerationYCtrl->setEnabled(!target_linear);
	mAcellerationZCtrl->setEnabled(!target_linear);

	mOmegaXCtrl->setEnabled(!target_linear);
	mOmegaYCtrl->setEnabled(!target_linear);
	mOmegaZCtrl->setEnabled(!target_linear);

	mAngleBeginCtrl->setEnabled(!(explode_pattern | drop_pattern));
	mAngleEndCtrl->setEnabled(!(explode_pattern | drop_pattern));
}

void ALFloaterParticleEditor::onClickClearTarget()
{
	mTargetKeyInput->clear();
	onParameterChange();
}

void ALFloaterParticleEditor::onClickTargetPicker()
{
	mPickTargetButton->setToggleState(TRUE);
	mPickTargetButton->setEnabled(FALSE);
	LLToolObjPicker::getInstance()->setExitCallback(onTargetPicked, this);
	LLToolMgr::getInstance()->setTransientTool(LLToolObjPicker::getInstance());
}

// static
void ALFloaterParticleEditor::onTargetPicked(void* userdata)
{
	ALFloaterParticleEditor* self = static_cast<ALFloaterParticleEditor*>(userdata);

	const LLUUID picked = LLToolObjPicker::getInstance()->getObjectID();

	LLToolMgr::getInstance()->clearTransientTool();

	self->mPickTargetButton->setEnabled(TRUE);
	self->mPickTargetButton->setToggleState(FALSE);

	if (picked.notNull())
	{
		self->mTargetKeyInput->setValue(picked.asString());
		self->onParameterChange();
	}
}

/* static */
std::string ALFloaterParticleEditor::lslVector(const F32 x, const F32 y, const F32 z)
{
	return llformat("<%f,%f,%f>", x, y, z);
}

/* static */
std::string ALFloaterParticleEditor::lslColor(const LLColor4& color)
{
	return lslVector(color.mV[VRED], color.mV[VGREEN], color.mV[VBLUE]);
}

std::string ALFloaterParticleEditor::createScript()
{
	std::string script(
"\
default\n\
{\n\
    state_entry()\n\
    {\n\
        llParticleSystem(\n\
        [\n\
            PSYS_SRC_PATTERN,[PATTERN],\n\
            PSYS_SRC_BURST_RADIUS,[BURST_RADIUS],\n\
            PSYS_SRC_ANGLE_BEGIN,[ANGLE_BEGIN],\n\
            PSYS_SRC_ANGLE_END,[ANGLE_END],\n\
            PSYS_SRC_TARGET_KEY,[TARGET_KEY],\n\
            PSYS_PART_START_COLOR,[START_COLOR],\n\
            PSYS_PART_END_COLOR,[END_COLOR],\n\
            PSYS_PART_START_ALPHA,[START_ALPHA],\n\
            PSYS_PART_END_ALPHA,[END_ALPHA],\n\
            PSYS_PART_START_GLOW,[START_GLOW],\n\
            PSYS_PART_END_GLOW,[END_GLOW],\n\
            PSYS_PART_BLEND_FUNC_SOURCE,[BLEND_FUNC_SOURCE],\n\
            PSYS_PART_BLEND_FUNC_DEST,[BLEND_FUNC_DEST],\n\
            PSYS_PART_START_SCALE,[START_SCALE],\n\
            PSYS_PART_END_SCALE,[END_SCALE],\n\
            PSYS_SRC_TEXTURE,\"[TEXTURE]\",\n\
            PSYS_SRC_MAX_AGE,[SOURCE_MAX_AGE],\n\
            PSYS_PART_MAX_AGE,[PART_MAX_AGE],\n\
            PSYS_SRC_BURST_RATE,[BURST_RATE],\n\
            PSYS_SRC_BURST_PART_COUNT,[BURST_COUNT],\n\
            PSYS_SRC_ACCEL,[ACCELERATION],\n\
            PSYS_SRC_OMEGA,[OMEGA],\n\
            PSYS_SRC_BURST_SPEED_MIN,[BURST_SPEED_MIN],\n\
            PSYS_SRC_BURST_SPEED_MAX,[BURST_SPEED_MAX],\n\
            PSYS_PART_FLAGS,\n\
                0[FLAGS]\n\
        ]);\n\
    }\n\
}\n");

	const LLUUID target_key = mTargetKeyInput->getValue().asUUID();
	std::string key_string = "llGetKey()";

	if (target_key.notNull() && target_key != mObject->getID())
	{
		key_string = "(key) \"" + target_key.asString() + "\"";
	}

	LLUUID texture_key = mTexture->getID();
	std::string texture_string;
	if (texture_key.notNull() && texture_key != IMG_DEFAULT && texture_key != mDefaultParticleTexture->getID())
	{
		texture_string = texture_key.asString();
	}

	LLStringUtil::replaceString(script, "[PATTERN]", sParticlePatterns.at(mPatternTypeCombo->getValue()).script_const);
	LLStringUtil::replaceString(script, "[BURST_RADIUS]", mBurstRadiusCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[ANGLE_BEGIN]", mAngleBeginCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[ANGLE_END]", mAngleEndCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[TARGET_KEY]", key_string);
	LLStringUtil::replaceString(script, "[START_COLOR]", lslColor(mStartColorSelector->get()));
	LLStringUtil::replaceString(script, "[END_COLOR]", lslColor(mEndColorSelector->get()));
	LLStringUtil::replaceString(script, "[START_ALPHA]", mStartAlphaCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[END_ALPHA]", mEndAlphaCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[START_GLOW]", mStartGlowCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[END_GLOW]", mEndGlowCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[START_SCALE]", lslVector(mScaleStartXCtrl->getValue().asReal(),
								       mScaleStartYCtrl->getValue().asReal(),
								       0.0f));
	LLStringUtil::replaceString(script, "[END_SCALE]", lslVector(mScaleEndXCtrl->getValue().asReal(),
								     mScaleEndYCtrl->getValue().asReal(),
								     0.0f));
	LLStringUtil::replaceString(script, "[TEXTURE]", texture_string);
	LLStringUtil::replaceString(script, "[SOURCE_MAX_AGE]", mSourceMaxAgeCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[PART_MAX_AGE]", mParticlesMaxAgeCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[BURST_RATE]", mBurstRateCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[BURST_COUNT]", mBurstCountCtrl->getValue());
	LLStringUtil::replaceString(script, "[ACCELERATION]", lslVector(mAcellerationXCtrl->getValue().asReal(),
									mAcellerationYCtrl->getValue().asReal(),
									mAcellerationZCtrl->getValue().asReal()));
	LLStringUtil::replaceString(script, "[OMEGA]", lslVector(mOmegaXCtrl->getValue().asReal(),
								 mOmegaYCtrl->getValue().asReal(),
								 mOmegaZCtrl->getValue().asReal()));
	LLStringUtil::replaceString(script, "[BURST_SPEED_MIN]", mBurstSpeedMinCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[BURST_SPEED_MAX]", mBurstSpeedMaxCtrl->getValue().asString());
	LLStringUtil::replaceString(script, "[BLEND_FUNC_SOURCE]", sParticleBlends.at(mBlendFuncSrcCombo->getValue().asString()).script_const);
	LLStringUtil::replaceString(script, "[BLEND_FUNC_DEST]", sParticleBlends.at(mBlendFuncDestCombo->getValue().asString()).script_const);

    const std::string delimiter = " |\n                ";
	std::string flags_string = "";

	if (mBounceCheckBox->getValue())
		flags_string += delimiter + "PSYS_PART_BOUNCE_MASK";
	if (mEmissiveCheckBox->getValue())
		flags_string += delimiter + "PSYS_PART_EMISSIVE_MASK";
	if (mFollowSourceCheckBox->getValue())
		flags_string += delimiter + "PSYS_PART_FOLLOW_SRC_MASK";
	if (mFollowVelocityCheckBox->getValue())
		flags_string += delimiter + "PSYS_PART_FOLLOW_VELOCITY_MASK";
	if (mInterpolateColorCheckBox->getValue())
		flags_string += delimiter + "PSYS_PART_INTERP_COLOR_MASK";
	if (mInterpolateScaleCheckBox->getValue())
		flags_string += delimiter + "PSYS_PART_INTERP_SCALE_MASK";
	if (mTargetLinearCheckBox->getValue())
		flags_string += delimiter + "PSYS_PART_TARGET_LINEAR_MASK";
	if (mTargetPositionCheckBox->getValue())
		flags_string += delimiter + "PSYS_PART_TARGET_POS_MASK";
	if (mWindCheckBox->getValue())
		flags_string += delimiter + "PSYS_PART_WIND_MASK";
	if (mRibbonCheckBox->getValue())
		flags_string += delimiter + "PSYS_PART_RIBBON_MASK";

	LLStringUtil::replaceString(script, "[FLAGS]", flags_string);
	LL_DEBUGS("ParticleEditor") << "\n" << script << LL_ENDL;

	return script;
}

void ALFloaterParticleEditor::onClickCopy()
{
    mChanged = false;
	const std::string script = createScript();
	if (!script.empty())
	{
		getWindow()->copyTextToClipboard(utf8str_to_wstring(script));
		LLNotificationsUtil::add("ParticleScriptCopiedToClipboard");
	}
}

void ALFloaterParticleEditor::injectScript()
{
    mChanged = false;
	const LLUUID categoryID = gInventory.findCategoryUUIDForType(LLFolderType::FT_LSL_TEXT);

	// if no valid folder found bail out and complain
	if (categoryID.isNull())
	{
		LLNotificationsUtil::add("ParticleScriptFindFolderFailed");
		return;
	}

	// setup permissions
	LLPermissions perm;
	perm.init(gAgentID, gAgentID, LLUUID::null, LLUUID::null);
	perm.initMasks(PERM_ALL, PERM_ALL, PERM_ALL, PERM_ALL, PERM_ALL);

	// create new script inventory item and wait for it to come back (callback)
	LLPointer<LLInventoryCallback> callback = new LLParticleScriptCreationCallback(this);
	create_inventory_item(
		gAgentID,
		gAgentSessionID,
		categoryID,
		LLTransactionID::tnull,
		PARTICLE_SCRIPT_NAME,
		LLStringUtil::null,
		LLAssetType::AT_LSL_TEXT,
		LLInventoryType::IT_LSL,
		NO_INV_SUBTYPE,
		perm.getMaskNextOwner(),
		callback);

	setCanClose(FALSE);
}

void ALFloaterParticleEditor::callbackReturned(const LLUUID& inventoryItemID)
{
	setCanClose(TRUE);

	if (inventoryItemID.isNull())
	{
		LLNotificationsUtil::add("ParticleScriptCreationFailed");
		return;
	}

	mParticleScriptInventoryItem = gInventory.getItem(inventoryItemID);
	if (!mParticleScriptInventoryItem)
	{
		LLNotificationsUtil::add("ParticleScriptNotFound");
		return;
	}
	gInventory.updateItem(mParticleScriptInventoryItem);
	gInventory.notifyObservers();

	//caps import 
	const std::string url = gAgent.getRegionCapability("UpdateScriptAgent");

	if (url.empty())
	{
		LLNotificationsUtil::add("ParticleScriptFailed");
		return;
	}

	
	const std::string script = createScript();

	LLBufferedAssetUploadInfo::taskUploadFinish_f proc =
		boost::bind(&ALFloaterParticleEditor::finishUpload, _1, _2, _3, _4, true, mObject->getID());
	LLResourceUploadInfo::ptr_t uploadInfo(new LLScriptAssetUpload(mObject->getID(), inventoryItemID,
		LLScriptAssetUpload::MONO, true, LLUUID::null, script, proc));
	LLViewerAssetUpload::EnqueueInventoryUpload(url, uploadInfo);

    if (mCloseAfterSave) closeFloater();
}

// ---------------------------------- Callbacks ----------------------------------

ALFloaterParticleEditor::LLParticleScriptCreationCallback::
LLParticleScriptCreationCallback(ALFloaterParticleEditor* editor)
	: mEditor(editor)
{
}

void ALFloaterParticleEditor::LLParticleScriptCreationCallback::fire(const LLUUID& inventoryItem)
{
	if (!gDisconnected && !LLAppViewer::instance()->quitRequested() && mEditor)
	{
		mEditor->callbackReturned(inventoryItem);
	}
}

/* static */
void ALFloaterParticleEditor::finishUpload(LLUUID itemId, LLUUID taskId, LLUUID newAssetId,
    LLSD response, bool isRunning, LLUUID objectId)
{
    // make sure there's still an object rezzed
    LLViewerObject* object = gObjectList.findObject(objectId);
    if (!object || object->isDead())
    {
        LL_WARNS("ParticleEditor") << "Failed to inject script in object: " << objectId.asString() << LL_ENDL;
        return;
    }
    auto* script = gInventory.getItem(itemId);
    object->saveScript(script, TRUE, FALSE);

    LLNotificationsUtil::add("ParticleScriptInjected");
}
