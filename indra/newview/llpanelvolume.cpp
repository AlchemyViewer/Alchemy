/** 
 * @file llpanelvolume.cpp
 * @brief Object editing (position, scale, etc.) in the tools floater
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

// file include
#include "llpanelvolume.h"

// linden library includes
#include "llclickaction.h"
#include "llerror.h"
#include "llfontgl.h"
#include "llflexibleobject.h"
#include "llmaterialtable.h"
#include "llpermissionsflags.h"
#include "llstring.h"
#include "llvolume.h"
#include "m3math.h"
#include "material_codes.h"

// project includes
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcolorswatch.h"
#include "lltexturectrl.h"
#include "llcombobox.h"
//#include "llfirstuse.h"
#include "llfocusmgr.h"
#include "llmanipscale.h"
#include "llpreviewscript.h"
#include "llresmgr.h"
#include "llselectmgr.h"
#include "llspinctrl.h"
#include "lltextbox.h"
#include "lltool.h"
#include "lltoolcomp.h"
#include "lltoolmgr.h"
#include "lltrans.h"
#include "llui.h"
#include "llviewerobject.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llvovolume.h"
#include "llworld.h"
#include "pipeline.h"
#include "llviewershadermgr.h"
#include "llnotificationsutil.h"

#include "lldrawpool.h"
#include "lluictrlfactory.h"

// For mesh physics
#include "llagent.h"
#include "llviewercontrol.h"
#include "llmeshrepository.h"

#include "llvoavatarself.h"

#include <boost/bind.hpp>


const F32 DEFAULT_GRAVITY_MULTIPLIER = 1.f;
const F32 DEFAULT_DENSITY = 1000.f;

// "Features" Tab
BOOL	LLPanelVolume::postBuild()
{
    mLabelEditObject = getChild<LLTextBox>("edit_object");
    mLabelSelectSingle = getChild<LLTextBox>("select_single");

    mCheckAnimesh = getChild<LLCheckBoxCtrl>("Animated Mesh Checkbox Ctrl");
    mCheckAnimesh->setCommitCallback(std::bind(&LLPanelVolume::onCommitAnimatedMeshCheckbox, this, std::placeholders::_1, this));

	// Flexible Objects Parameters
	{
        mCheckFlexible1D = getChild<LLCheckBoxCtrl>("Flexible1D Checkbox Ctrl");
        mCheckFlexible1D->setCommitCallback(std::bind(&LLPanelVolume::onCommitIsFlexible, this, std::placeholders::_1, this));

		mSpinSections = getChild<LLSpinCtrl>("FlexNumSections");
        mSpinSections->setValidateBeforeCommit(precommitValidate);
        mSpinSections->setCommitCallback(std::bind(onCommitFlexible, std::placeholders::_1, this));

        mSpinGravity  = getChild<LLSpinCtrl>("FlexGravity");
        mSpinGravity->setValidateBeforeCommit(precommitValidate);
        mSpinGravity->setCommitCallback(std::bind(onCommitFlexible, std::placeholders::_1, this));

        mSpinTension  = getChild<LLSpinCtrl>("FlexTension");
        mSpinTension->setValidateBeforeCommit(precommitValidate);
        mSpinTension->setCommitCallback(std::bind(onCommitFlexible, std::placeholders::_1, this));

        mSpinFriction = getChild<LLSpinCtrl>("FlexFriction");
        mSpinFriction->setValidateBeforeCommit(precommitValidate);
        mSpinFriction->setCommitCallback(std::bind(onCommitFlexible, std::placeholders::_1, this));

        mSpinWind = getChild<LLSpinCtrl>("FlexWind");
        mSpinWind->setValidateBeforeCommit(precommitValidate);
        mSpinWind->setCommitCallback(std::bind(onCommitFlexible, std::placeholders::_1, this));

        mSpinForceX = getChild<LLSpinCtrl>("FlexForceX");
        mSpinForceX->setValidateBeforeCommit(precommitValidate);
        mSpinForceX->setCommitCallback(std::bind(onCommitFlexible, std::placeholders::_1, this));

        mSpinForceY = getChild<LLSpinCtrl>("FlexForceY");
        mSpinForceY->setValidateBeforeCommit(precommitValidate);
        mSpinForceY->setCommitCallback(std::bind(onCommitFlexible, std::placeholders::_1, this));

        mSpinForceZ = getChild<LLSpinCtrl>("FlexForceZ");
        mSpinForceZ->setValidateBeforeCommit(precommitValidate);
        mSpinForceZ->setCommitCallback(std::bind(onCommitFlexible, std::placeholders::_1, this));
	}

	// LIGHT Parameters
	{
        mCheckLight = getChild<LLCheckBoxCtrl>("Light Checkbox Ctrl");
        mCheckLight->setCommitCallback(std::bind(onCommitIsLight, std::placeholders::_1, this));

		mLightColorSwatch = getChild<LLColorSwatchCtrl>("colorswatch");
        mLightColorSwatch->setOnCancelCallback(std::bind(&LLPanelVolume::onLightCancelColor, this, std::placeholders::_2));
        mLightColorSwatch->setOnSelectCallback(std::bind(&LLPanelVolume::onLightSelectColor, this, std::placeholders::_2));
        mLightColorSwatch->setCommitCallback(std::bind(onCommitLight, std::placeholders::_1, this));

        mLightTextureCtrl = getChild<LLTextureCtrl>("light texture control");
        mLightTextureCtrl->setOnCancelCallback(boost::bind(&LLPanelVolume::onLightCancelTexture, this, _2));
        mLightTextureCtrl->setOnSelectCallback(boost::bind(&LLPanelVolume::onLightSelectTexture, this, _2));
        mLightTextureCtrl->setCommitCallback(std::bind(onCommitLight, std::placeholders::_1, this));

		mLightIntensity = getChild<LLSpinCtrl>("Light Intensity");
        mLightIntensity->setValidateBeforeCommit(precommitValidate);
        mLightIntensity->setCommitCallback(std::bind(onCommitLight, std::placeholders::_1, this));

        mLightRadius = getChild<LLSpinCtrl>("Light Radius");
        mLightRadius->setValidateBeforeCommit(precommitValidate);
        mLightRadius->setCommitCallback(std::bind(onCommitLight, std::placeholders::_1, this));

		mLightFalloff = getChild<LLSpinCtrl>("Light Falloff");
        mLightFalloff->setValidateBeforeCommit(precommitValidate);
        mLightFalloff->setCommitCallback(std::bind(onCommitLight, std::placeholders::_1, this));

		mLightFOV = getChild<LLSpinCtrl>("Light FOV");
        mLightFOV->setValidateBeforeCommit(precommitValidate);
        mLightFOV->setCommitCallback(std::bind(onCommitLight, std::placeholders::_1, this));

		mLightFocus = getChild<LLSpinCtrl>("Light Focus");
        mLightFocus->setValidateBeforeCommit(precommitValidate);
        mLightFocus->setCommitCallback(std::bind(onCommitLight, std::placeholders::_1, this));

        mLightAmbiance = getChild<LLSpinCtrl>("Light Ambiance");
        mLightAmbiance->setValidateBeforeCommit(precommitValidate);
        mLightAmbiance->setCommitCallback(std::bind(onCommitLight, std::placeholders::_1, this));
	}
	
	// PHYSICS Parameters
	{
        mLabelPhysicsShapeType = getChild<LLTextBox>("label physicsshapetype");

		// PhysicsShapeType combobox
		mComboPhysicsShapeType = getChild<LLComboBox>("Physics Shape Type Combo Ctrl");
		mComboPhysicsShapeType->setCommitCallback(boost::bind(&LLPanelVolume::sendPhysicsShapeType, this, _1, mComboPhysicsShapeType));
	
		// PhysicsGravity
		mSpinPhysicsGravity = getChild<LLSpinCtrl>("Physics Gravity");
		mSpinPhysicsGravity->setCommitCallback(boost::bind(&LLPanelVolume::sendPhysicsGravity, this, _1, mSpinPhysicsGravity));

		// PhysicsFriction
		mSpinPhysicsFriction = getChild<LLSpinCtrl>("Physics Friction");
		mSpinPhysicsFriction->setCommitCallback(boost::bind(&LLPanelVolume::sendPhysicsFriction, this, _1, mSpinPhysicsFriction));

		// PhysicsDensity
		mSpinPhysicsDensity = getChild<LLSpinCtrl>("Physics Density");
		mSpinPhysicsDensity->setCommitCallback(boost::bind(&LLPanelVolume::sendPhysicsDensity, this, _1, mSpinPhysicsDensity));

		// PhysicsRestitution
		mSpinPhysicsRestitution = getChild<LLSpinCtrl>("Physics Restitution");
		mSpinPhysicsRestitution->setCommitCallback(boost::bind(&LLPanelVolume::sendPhysicsRestitution, this, _1, mSpinPhysicsRestitution));
	}

	std::map<std::string, std::string> material_name_map;
	material_name_map["Stone"]= LLTrans::getString("Stone");
	material_name_map["Metal"]= LLTrans::getString("Metal");	
	material_name_map["Glass"]= LLTrans::getString("Glass");	
	material_name_map["Wood"]= LLTrans::getString("Wood");	
	material_name_map["Flesh"]= LLTrans::getString("Flesh");
	material_name_map["Plastic"]= LLTrans::getString("Plastic");
	material_name_map["Rubber"]= LLTrans::getString("Rubber");	
	material_name_map["Light"]= LLTrans::getString("Light");		
	
	LLMaterialTable::basic.initTableTransNames(material_name_map);

	// material type popup
	mComboMaterial = getChild<LLComboBox>("material");
    mComboMaterial->setCommitCallback(std::bind(onCommitMaterial, std::placeholders::_1, this));
	mComboMaterial->removeall();

	for (LLMaterialTable::info_list_t::iterator iter = LLMaterialTable::basic.mMaterialInfoList.begin();
		 iter != LLMaterialTable::basic.mMaterialInfoList.end(); ++iter)
	{
		LLMaterialInfo* minfop = *iter;
		if (minfop->mMCode != LL_MCODE_LIGHT)
		{
			mComboMaterial->add(minfop->mName);  
		}
	}
	mComboMaterialItemCount = mComboMaterial->getItemCount();
	
	// Start with everyone disabled
	clearCtrls();

	return TRUE;
}

LLPanelVolume::LLPanelVolume()
	: LLPanel(),
	  mComboMaterialItemCount(0)
{
	setMouseOpaque(FALSE);

}


LLPanelVolume::~LLPanelVolume()
{
	// Children all cleaned up by default view destructor.
}

void LLPanelVolume::getState( )
{
	LLViewerObject* objectp = LLSelectMgr::getInstance()->getSelection()->getFirstRootObject();
	LLViewerObject* root_objectp = objectp;
	if(!objectp)
	{
		objectp = LLSelectMgr::getInstance()->getSelection()->getFirstObject();
		// *FIX: shouldn't we just keep the child?
		if (objectp)
		{
			LLViewerObject* parentp = objectp->getRootEdit();

			if (parentp)
			{
				root_objectp = parentp;
			}
			else
			{
				root_objectp = objectp;
			}
		}
	}

	LLVOVolume *volobjp = NULL;
	if ( objectp && (objectp->getPCode() == LL_PCODE_VOLUME))
	{
		volobjp = (LLVOVolume *)objectp;
	}
	LLVOVolume *root_volobjp = NULL;
    if (root_objectp && (root_objectp->getPCode() == LL_PCODE_VOLUME))
    {
        root_volobjp  = (LLVOVolume *)root_objectp;
    }
	
	if( !objectp )
	{
		//forfeit focus
		if (gFocusMgr.childHasKeyboardFocus(this))
		{
			gFocusMgr.setKeyboardFocus(NULL);
		}

		// Disable all text input fields
		clearCtrls();

		return;
	}

	LLUUID owner_id;
	std::string owner_name;
	LLSelectMgr::getInstance()->selectGetOwner(owner_id, owner_name);

	// BUG? Check for all objects being editable?
	BOOL editable = root_objectp->permModify() && !root_objectp->isPermanentEnforced();
	BOOL single_volume = LLSelectMgr::getInstance()->selectionAllPCode( LL_PCODE_VOLUME )
		&& LLSelectMgr::getInstance()->getSelection()->getObjectCount() == 1;
    BOOL single_root_volume = LLSelectMgr::getInstance()->selectionAllPCode( LL_PCODE_VOLUME ) && 
        LLSelectMgr::getInstance()->getSelection()->getRootObjectCount() == 1;

	// Select Single Message
	if (single_volume)
	{
        mLabelEditObject->setVisible(true);
        mLabelEditObject->setEnabled(true);
        mLabelSelectSingle->setVisible(false);
	}
	else
	{	
		mLabelEditObject->setVisible(false);
        mLabelSelectSingle->setVisible(true);
        mLabelSelectSingle->setEnabled(true);
	}
	
	// Light properties
	BOOL is_light = volobjp && volobjp->getIsLight();
    mCheckLight->setValue(is_light);
    mCheckLight->setEnabled(editable && single_volume && volobjp);
	
	if (is_light && editable && single_volume)
	{
		//mLabelColor		 ->setEnabled( TRUE );

		mLightColorSwatch->setEnabled( TRUE );
		mLightColorSwatch->setValid( TRUE );
		mLightColorSwatch->set(volobjp->getLightSRGBBaseColor());

		mLightTextureCtrl->setEnabled(TRUE);
		mLightTextureCtrl->setValid(TRUE);
		mLightTextureCtrl->setImageAssetID(volobjp->getLightTextureID());

		mLightIntensity->setEnabled(true);
        mLightRadius->setEnabled(true);
        mLightFalloff->setEnabled(true);

		mLightFOV->setEnabled(true);
        mLightFocus->setEnabled(true);
        mLightAmbiance->setEnabled(true);
		
		mLightIntensity->setValue(volobjp->getLightIntensity());
        mLightRadius->setValue(volobjp->getLightRadius());
        mLightFalloff->setValue(volobjp->getLightFalloff());

		LLVector3 params = volobjp->getSpotLightParams();
        mLightFOV->setValue(params.mV[0]);
        mLightFocus->setValue(params.mV[1]);
        mLightAmbiance->setValue(params.mV[2]);

		mLightSavedColor = volobjp->getLightSRGBBaseColor();
	}
	else
	{
        mLightIntensity->clear();
        mLightRadius->clear();
        mLightFalloff->clear();

		mLightColorSwatch->setEnabled( FALSE );
		mLightColorSwatch->setValid( FALSE );

		mLightTextureCtrl->setEnabled(FALSE);
		mLightTextureCtrl->setValid(FALSE);

		mLightIntensity->setEnabled(false);
        mLightRadius->setEnabled(false);
        mLightFalloff->setEnabled(false);

		mLightFOV->setEnabled(false);
        mLightFocus->setEnabled(false);
        mLightAmbiance->setEnabled(false);
	}

    // Animated Mesh
	BOOL is_animated_mesh = single_root_volume && root_volobjp && root_volobjp->isAnimatedObject();
    mCheckAnimesh->setValue(is_animated_mesh);
    BOOL enabled_animated_object_box = FALSE;
    if (root_volobjp && root_volobjp == volobjp)
    {
        enabled_animated_object_box = single_root_volume && root_volobjp && root_volobjp->canBeAnimatedObject() && editable; 
#if 0
        if (!enabled_animated_object_box)
        {
            LL_INFOS() << "not enabled: srv " << single_root_volume << " root_volobjp " << (bool) root_volobjp << LL_ENDL;
            if (root_volobjp)
            {
                LL_INFOS() << " cba " << root_volobjp->canBeAnimatedObject()
                           << " editable " << editable << " permModify() " << root_volobjp->permModify()
                           << " ispermenf " << root_volobjp->isPermanentEnforced() << LL_ENDL;
            }
        }
#endif
        if (enabled_animated_object_box && !is_animated_mesh && 
            root_volobjp->isAttachment() && !gAgentAvatarp->canAttachMoreAnimatedObjects())
        {
            // Turning this attachment animated would cause us to exceed the limit.
            enabled_animated_object_box = false;
        }
    }
    mCheckAnimesh->setEnabled(enabled_animated_object_box);
	
	//refresh any bakes
	if (root_volobjp)
	{
		root_volobjp->refreshBakeTexture();

		LLViewerObject::const_child_list_t& child_list = root_volobjp->getChildren();
        for (LLViewerObject* objectp : child_list)
		{
			if (objectp)
			{
				objectp->refreshBakeTexture();
			}
		}

		if (gAgentAvatarp)
		{
			gAgentAvatarp->updateMeshVisibility();
		}
	}
	

	// Flexible properties
	BOOL is_flexible = volobjp && volobjp->isFlexible();
    mCheckFlexible1D->setValue(is_flexible);
	if (is_flexible || (volobjp && volobjp->canBeFlexible()))
	{
        mCheckFlexible1D->setEnabled(editable && single_volume && volobjp && !volobjp->isMesh() && !objectp->isPermanentEnforced());
	}
	else
	{
        mCheckFlexible1D->setEnabled(false);
	}
	if (is_flexible && editable && single_volume)
	{
        mSpinSections->setVisible(true);
		mSpinGravity->setVisible(true);
        mSpinTension->setVisible(true);
        mSpinFriction->setVisible(true);
        mSpinWind->setVisible(true);
        mSpinForceX->setVisible(true);
        mSpinForceY->setVisible(true);
        mSpinForceZ->setVisible(true);

		mSpinSections->setEnabled(true);
        mSpinGravity->setEnabled(true);
        mSpinTension->setEnabled(true);
        mSpinFriction->setEnabled(true);
        mSpinWind->setEnabled(true);
        mSpinForceX->setEnabled(true);
        mSpinForceY->setEnabled(true);
        mSpinForceZ->setEnabled(true);

		LLFlexibleObjectData *attributes = (LLFlexibleObjectData *)objectp->getFlexibleObjectData();
		
		mSpinSections->setValue((F32) attributes->getSimulateLOD());
        mSpinGravity->setValue(attributes->getGravity());
        mSpinTension->setValue(attributes->getTension());
        mSpinFriction->setValue(attributes->getAirFriction());
        mSpinWind->setValue(attributes->getWindSensitivity());
        mSpinForceX->setValue(attributes->getUserForce().mV[VX]);
        mSpinForceY->setValue(attributes->getUserForce().mV[VY]);
        mSpinForceZ->setValue(attributes->getUserForce().mV[VZ]);
	}
	else
	{
        mSpinSections->clear();
        mSpinGravity->clear();
        mSpinTension->clear();
		mSpinFriction->clear();
		mSpinWind->clear();
		mSpinForceX->clear();
        mSpinForceY->clear();
        mSpinForceZ->clear();

		mSpinSections->setEnabled(false);
        mSpinGravity->setEnabled(false);
        mSpinTension->setEnabled(false);
        mSpinFriction->setEnabled(false);
        mSpinWind->setEnabled(false);
        mSpinForceX->setEnabled(false);
        mSpinForceY->setEnabled(false);
        mSpinForceZ->setEnabled(false);
	}
	
	// Material properties

	// Update material part
	// slightly inefficient - materials are unique per object, not per TE
	U8 material_code = 0;
	struct f : public LLSelectedTEGetFunctor<U8>
	{
		U8 get(LLViewerObject* object, S32 te)
		{
			return object->getMaterial();
		}
	} func;
	bool material_same = LLSelectMgr::getInstance()->getSelection()->getSelectedTEValue( &func, material_code );
	std::string LEGACY_FULLBRIGHT_DESC = LLTrans::getString("Fullbright");
	if (editable && single_volume && material_same)
	{
		mComboMaterial->setEnabled( TRUE );
		if (material_code == LL_MCODE_LIGHT)
		{
			if (mComboMaterial->getItemCount() == mComboMaterialItemCount)
			{
				mComboMaterial->add(LEGACY_FULLBRIGHT_DESC);
			}
			mComboMaterial->setSimple(LEGACY_FULLBRIGHT_DESC);
		}
		else
		{
			if (mComboMaterial->getItemCount() != mComboMaterialItemCount)
			{
				mComboMaterial->remove(LEGACY_FULLBRIGHT_DESC);
			}
			
			mComboMaterial->setSimple(std::string(LLMaterialTable::basic.getName(material_code)));
		}
	}
	else
	{
		mComboMaterial->setEnabled( FALSE );
	}

	// Physics properties
	
	mSpinPhysicsGravity->set(objectp->getPhysicsGravity());
	mSpinPhysicsGravity->setEnabled(editable);

	mSpinPhysicsFriction->set(objectp->getPhysicsFriction());
	mSpinPhysicsFriction->setEnabled(editable);
	
	mSpinPhysicsDensity->set(objectp->getPhysicsDensity());
	mSpinPhysicsDensity->setEnabled(editable);
	
	mSpinPhysicsRestitution->set(objectp->getPhysicsRestitution());
	mSpinPhysicsRestitution->setEnabled(editable);

	// update the physics shape combo to include allowed physics shapes
	mComboPhysicsShapeType->removeall();
	mComboPhysicsShapeType->add(getString("None"), LLSD(1));

	BOOL isMesh = FALSE;
	LLSculptParams *sculpt_params = (LLSculptParams *)objectp->getSculptParams();
	if (sculpt_params)
	{
		U8 sculpt_type = sculpt_params->getSculptType();
		U8 sculpt_stitching = sculpt_type & LL_SCULPT_TYPE_MASK;
		isMesh = (sculpt_stitching == LL_SCULPT_TYPE_MESH);
	}

	if(isMesh && objectp)
	{
		const LLVolumeParams &volume_params = objectp->getVolume()->getParams();
		LLUUID mesh_id = volume_params.getSculptID();
		if(gMeshRepo.hasPhysicsShape(mesh_id))
		{
			// if a mesh contains an uploaded or decomposed physics mesh,
			// allow 'Prim'
			mComboPhysicsShapeType->add(getString("Prim"), LLSD(0));			
		}
	}
	else
	{
		// simple prims always allow physics shape prim
		mComboPhysicsShapeType->add(getString("Prim"), LLSD(0));	
	}

	mComboPhysicsShapeType->add(getString("Convex Hull"), LLSD(2));	
	mComboPhysicsShapeType->setValue(LLSD(objectp->getPhysicsShapeType()));
	mComboPhysicsShapeType->setEnabled(editable && !objectp->isPermanentEnforced() && ((root_objectp == NULL) || !root_objectp->isPermanentEnforced()));

	mObject = objectp;
	mRootObject = root_objectp;
}

// static
bool LLPanelVolume::precommitValidate( const LLSD& data )
{
	// TODO: Richard will fill this in later.  
	return TRUE; // FALSE means that validation failed and new value should not be commited.
}


void LLPanelVolume::refresh()
{
	getState();
	if (mObject.notNull() && mObject->isDead())
	{
		mObject = NULL;
	}

	if (mRootObject.notNull() && mRootObject->isDead())
	{
		mRootObject = NULL;
	}

	BOOL visible = LLViewerShaderMgr::instance()->getShaderLevel(LLViewerShaderMgr::SHADER_DEFERRED) > 0 ? TRUE : FALSE;

	mLightFOV->setVisible(visible);
    mLightFocus->setVisible(visible);
    mLightAmbiance->setVisible(visible);
	mLightTextureCtrl->setVisible( visible);

	bool enable_mesh = false;

	LLSD sim_features;
	LLViewerRegion *region = gAgent.getRegion();
	if(region)
	{
		LLSD sim_features;
		region->getSimulatorFeatures(sim_features);		 
		enable_mesh = sim_features.has("PhysicsShapeTypes");
	}
    mLabelPhysicsShapeType->setVisible(enable_mesh);
    mComboPhysicsShapeType->setVisible(enable_mesh);
    mSpinPhysicsGravity->setVisible(enable_mesh);
    mSpinPhysicsFriction->setVisible(enable_mesh);
    mSpinPhysicsDensity->setVisible(enable_mesh);
    mSpinPhysicsRestitution->setVisible(enable_mesh);
	
    /* TODO: add/remove individual physics shape types as per the PhysicsShapeTypes simulator features */
}


void LLPanelVolume::draw()
{
	LLPanel::draw();
}

// virtual
void LLPanelVolume::clearCtrls()
{
	LLPanel::clearCtrls();

	mLabelSelectSingle->setEnabled(false);
    mLabelSelectSingle->setVisible(true);
	mLabelEditObject->setEnabled(false);
    mLabelEditObject->setVisible(false);
    mCheckLight->setEnabled(false);

	mLightColorSwatch->setEnabled( FALSE );
	mLightColorSwatch->setValid( FALSE );
	mLightTextureCtrl->setEnabled( FALSE );
	mLightTextureCtrl->setValid( FALSE );

	mLightIntensity->setEnabled(false);
    mLightRadius->setEnabled(false);
    mLightFalloff->setEnabled(false);
	mLightFOV->setEnabled(false);
    mLightFocus->setEnabled(false);
    mLightAmbiance->setEnabled(false);

    mCheckAnimesh->setEnabled(false);
    mCheckFlexible1D->setEnabled(false);
    mSpinSections->setEnabled(false);
	mSpinGravity->setEnabled(false);
    mSpinTension->setEnabled(false);
	mSpinFriction->setEnabled(false);
	mSpinWind->setEnabled(false);
	mSpinForceX->setEnabled(false);
    mSpinForceY->setEnabled(false);
    mSpinForceZ->setEnabled(false);

	mSpinPhysicsGravity->setEnabled(FALSE);
	mSpinPhysicsFriction->setEnabled(FALSE);
	mSpinPhysicsDensity->setEnabled(FALSE);
	mSpinPhysicsRestitution->setEnabled(FALSE);

	mComboMaterial->setEnabled( FALSE );
}

//
// Static functions
//

void LLPanelVolume::sendIsLight()
{
	LLViewerObject* objectp = mObject;
	if (!objectp || (objectp->getPCode() != LL_PCODE_VOLUME))
	{
		return;
	}	
	LLVOVolume *volobjp = (LLVOVolume *)objectp;
	
	BOOL value = mCheckLight->get();
	volobjp->setIsLight(value);
	LL_INFOS() << "update light sent" << LL_ENDL;
}

void LLPanelVolume::sendIsFlexible()
{
	LLViewerObject* objectp = mObject;
	if (!objectp || (objectp->getPCode() != LL_PCODE_VOLUME))
	{
		return;
	}	
	LLVOVolume *volobjp = (LLVOVolume *)objectp;
	
	BOOL is_flexible = mCheckFlexible1D->getValue();
	//BOOL is_flexible = mCheckFlexible1D->get();

	if (is_flexible)
	{
		//LLFirstUse::useFlexible();

		if (objectp->getClickAction() == CLICK_ACTION_SIT)
		{
			LLSelectMgr::getInstance()->selectionSetClickAction(CLICK_ACTION_NONE);
		}

	}

	if (volobjp->setIsFlexible(is_flexible))
	{
		mObject->sendShapeUpdate();
		LLSelectMgr::getInstance()->selectionUpdatePhantom(volobjp->flagPhantom());
	}

	LL_INFOS() << "update flexible sent" << LL_ENDL;
}

void LLPanelVolume::sendPhysicsShapeType(LLUICtrl* ctrl, void* userdata)
{
	U8 type = ctrl->getValue().asInteger();
	LLSelectMgr::getInstance()->selectionSetPhysicsType(type);

	refreshCost();
}

void LLPanelVolume::sendPhysicsGravity(LLUICtrl* ctrl, void* userdata)
{
	F32 val = ctrl->getValue().asReal();
	LLSelectMgr::getInstance()->selectionSetGravity(val);
}

void LLPanelVolume::sendPhysicsFriction(LLUICtrl* ctrl, void* userdata)
{
	F32 val = ctrl->getValue().asReal();
	LLSelectMgr::getInstance()->selectionSetFriction(val);
}

void LLPanelVolume::sendPhysicsRestitution(LLUICtrl* ctrl, void* userdata)
{
	F32 val = ctrl->getValue().asReal();
	LLSelectMgr::getInstance()->selectionSetRestitution(val);
}

void LLPanelVolume::sendPhysicsDensity(LLUICtrl* ctrl, void* userdata)
{
	F32 val = ctrl->getValue().asReal();
	LLSelectMgr::getInstance()->selectionSetDensity(val);
}

void LLPanelVolume::refreshCost()
{
	LLViewerObject* obj = LLSelectMgr::getInstance()->getSelection()->getFirstObject();
	
	if (obj)
	{
		obj->getObjectCost();
	}
}

void LLPanelVolume::onLightCancelColor(const LLSD& data)
{
	mLightColorSwatch->setColor(mLightSavedColor);
	onLightSelectColor(data);
}

void LLPanelVolume::onLightCancelTexture(const LLSD& data)
{
	LLVOVolume *volobjp = (LLVOVolume *) mObject.get();

	if (volobjp)
	{
		// Cancel the light texture as requested
		// NORSPEC-292
        //
        // Texture picker triggers cancel both in case of actual cancel and in case of
        // selection of "None" texture.
        LLUUID tex_id = mLightTextureCtrl->getImageAssetID();
        bool is_spotlight = volobjp->isLightSpotlight();
        volobjp->setLightTextureID(tex_id); //updates spotlight

        if (!is_spotlight && tex_id.notNull())
        {
            LLVector3 spot_params = volobjp->getSpotLightParams();
            mLightFOV->setValue(spot_params.mV[0]);
            mLightFocus->setValue(spot_params.mV[1]);
            mLightAmbiance->setValue(spot_params.mV[2]);
        }
	}
}

void LLPanelVolume::onLightSelectColor(const LLSD& data)
{
	LLViewerObject* objectp = mObject;
	if (!objectp || (objectp->getPCode() != LL_PCODE_VOLUME))
	{
		return;
	}	
	LLVOVolume *volobjp = (LLVOVolume *)objectp;

	LLColor4	clr = mLightColorSwatch->get();
	LLColor3	clr3( clr );
	volobjp->setLightSRGBColor(clr3);
	mLightSavedColor = clr;
}

void LLPanelVolume::onLightSelectTexture(const LLSD& data)
{
	if (mObject.isNull() || (mObject->getPCode() != LL_PCODE_VOLUME))
	{
		return;
	}	
	LLVOVolume *volobjp = (LLVOVolume *) mObject.get();

	LLUUID id = mLightTextureCtrl->getImageAssetID();
	volobjp->setLightTextureID(id);
}

// static
void LLPanelVolume::onCommitMaterial( LLUICtrl* ctrl, void* userdata )
{
	LLPanelVolume* self = (LLPanelVolume*)userdata;
	LLComboBox* box = (LLComboBox*) ctrl;

	if (box)
	{
		// apply the currently selected material to the object
		const std::string& material_name = box->getSimple();
		std::string LEGACY_FULLBRIGHT_DESC = LLTrans::getString("Fullbright");
		if (material_name != LEGACY_FULLBRIGHT_DESC)
		{
			U8 material_code = LLMaterialTable::basic.getMCode(material_name);
			if (self)
			{
				LLViewerObject* objectp = self->mObject;
				if (objectp)
				{
					objectp->setPhysicsGravity(DEFAULT_GRAVITY_MULTIPLIER);
					objectp->setPhysicsFriction(LLMaterialTable::basic.getFriction(material_code));
					//currently density is always set to 1000 serverside regardless of chosen material,
					//actual material density should be used here, if this behavior change
					objectp->setPhysicsDensity(DEFAULT_DENSITY);
					objectp->setPhysicsRestitution(LLMaterialTable::basic.getRestitution(material_code));
				}
			}
			LLSelectMgr::getInstance()->selectionSetMaterial(material_code);
		}
	}
}

// static
void LLPanelVolume::onCommitLight( LLUICtrl* ctrl, void* userdata )
{
	LLPanelVolume* self = (LLPanelVolume*) userdata;
	LLViewerObject* objectp = self->mObject;
	if (!objectp || (objectp->getPCode() != LL_PCODE_VOLUME))
	{
		return;
	}	
	LLVOVolume *volobjp = (LLVOVolume *)objectp;
	
	
	volobjp->setLightIntensity((F32) self->mLightIntensity->getValue().asReal());
    volobjp->setLightRadius((F32) self->mLightRadius->getValue().asReal());
    volobjp->setLightFalloff((F32) self->mLightFalloff->getValue().asReal());

	LLColor4	clr = self->mLightColorSwatch->get();
	volobjp->setLightSRGBColor(LLColor3(clr));

	{
		LLUUID id = self->mLightTextureCtrl->getImageAssetID();
		if (id.notNull())
		{
			if (!volobjp->isLightSpotlight())
			{ //this commit is making this a spot light, set UI to default params
				volobjp->setLightTextureID(id);
				LLVector3 spot_params = volobjp->getSpotLightParams();
                self->mLightFOV->setValue(spot_params.mV[0]);
                self->mLightFocus->setValue(spot_params.mV[1]);
                self->mLightAmbiance->setValue(spot_params.mV[2]);
			}
			else
			{ //modifying existing params, this time volobjp won't change params on its own.
                if (volobjp->getLightTextureID() != id)
                {
                    volobjp->setLightTextureID(id);
                }

				LLVector3 spot_params;
                spot_params.mV[0] = (F32) self->mLightFOV->getValue().asReal();
                spot_params.mV[1] = (F32) self->mLightFocus->getValue().asReal();
                spot_params.mV[2] = (F32) self->mLightAmbiance->getValue().asReal();
				volobjp->setSpotLightParams(spot_params);
			}
		}
		else if (volobjp->isLightSpotlight())
		{ //no longer a spot light
			volobjp->setLightTextureID(id);
			//self->mLightFOV->setEnabled(FALSE);
			//self->mLightFocus->setEnabled(FALSE);
			//self->mLightAmbiance->setEnabled(FALSE);
		}
	}

	
}

// static
void LLPanelVolume::onCommitIsLight( LLUICtrl* ctrl, void* userdata )
{
	LLPanelVolume* self = (LLPanelVolume*) userdata;
	self->sendIsLight();
}

//----------------------------------------------------------------------------

// static
void LLPanelVolume::onCommitFlexible( LLUICtrl* ctrl, void* userdata )
{
	LLPanelVolume* self = (LLPanelVolume*) userdata;
	LLViewerObject* objectp = self->mObject;
	if (!objectp || (objectp->getPCode() != LL_PCODE_VOLUME))
	{
		return;
	}
	
	LLFlexibleObjectData *attributes = (LLFlexibleObjectData *)objectp->getFlexibleObjectData();
	if (attributes)
	{
		LLFlexibleObjectData new_attributes;
		new_attributes = *attributes;


		new_attributes.setSimulateLOD(self->mSpinSections->getValue().asInteger());  //(S32)self->mSpinSections->get());
		new_attributes.setGravity((F32)self->mSpinGravity->getValue().asReal());
		new_attributes.setTension((F32)self->mSpinTension->getValue().asReal());
		new_attributes.setAirFriction((F32)self->mSpinFriction->getValue().asReal());
		new_attributes.setWindSensitivity((F32)self->mSpinWind->getValue().asReal());
		F32 fx = (F32)self->mSpinForceX->getValue().asReal();
        F32       fy = (F32) self->mSpinForceY->getValue().asReal();
        F32       fz = (F32) self->mSpinForceZ->getValue().asReal();
		LLVector3 force(fx,fy,fz);

		new_attributes.setUserForce(force);
		objectp->setParameterEntry(LLNetworkData::PARAMS_FLEXIBLE, new_attributes, true);
	}

	// Values may fail validation
	self->refresh();
}

void LLPanelVolume::onCommitAnimatedMeshCheckbox(LLUICtrl *, void*)
{
	LLViewerObject* objectp = mObject;
	if (!objectp || (objectp->getPCode() != LL_PCODE_VOLUME))
	{
		return;
    }
	LLVOVolume *volobjp = (LLVOVolume *)objectp;
    BOOL        animated_mesh = mCheckAnimesh->getValue();
    U32 flags = volobjp->getExtendedMeshFlags();
    U32 new_flags = flags;
    if (animated_mesh)
    {
        new_flags |= LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG;
    }
    else
    {
        new_flags &= ~LLExtendedMeshParams::ANIMATED_MESH_ENABLED_FLAG;
    }
    if (new_flags != flags)
    {
        volobjp->setExtendedMeshFlags(new_flags);
    }

	//refresh any bakes
	if (volobjp)
	{
		volobjp->refreshBakeTexture();

		LLViewerObject::const_child_list_t& child_list = volobjp->getChildren();
        for (LLViewerObject* objectp : child_list)
		{
			if (objectp)
			{
				objectp->refreshBakeTexture();
			}
		}

		if (gAgentAvatarp)
		{
			gAgentAvatarp->updateMeshVisibility();
		}
	}
}

void LLPanelVolume::onCommitIsFlexible(LLUICtrl *, void*)
{
	if (mObject->flagObjectPermanent())
	{
		LLNotificationsUtil::add("PathfindingLinksets_ChangeToFlexiblePath", LLSD(), LLSD(), boost::bind(&LLPanelVolume::handleResponseChangeToFlexible, this, _1, _2));
	}
	else
	{
		sendIsFlexible();
	}
}

void LLPanelVolume::handleResponseChangeToFlexible(const LLSD &pNotification, const LLSD &pResponse)
{
	if (LLNotificationsUtil::getSelectedOption(pNotification, pResponse) == 0)
	{
		sendIsFlexible();
	}
	else
	{
        mCheckFlexible1D->setValue(FALSE);
	}
}
