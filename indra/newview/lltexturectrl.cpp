/** 
 * @file lltexturectrl.cpp
 * @author Richard Nelson, James Cook
 * @brief LLTextureCtrl class implementation including related functions
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "lltexturectrl.h"

#include "llagent.h"
#include "llviewertexturelist.h"
#include "llselectmgr.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llbutton.h"
#include "lldraghandle.h"
#include "llfolderviewmodel.h"
#include "llinventory.h"
#include "llinventoryfunctions.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llinventoryobserver.h"
#include "llinventorypanel.h"
#include "llui.h"
#include "llviewerinventory.h"
#include "llpermissions.h"
#include "llpreviewtexture.h"
#include "llassetstorage.h"
#include "lltextbox.h"
#include "llfloatertexturepicker.h"

#include "llviewerwindow.h"
#include "llviewerobject.h"
#include "llviewercontrol.h"
#include "lluictrlfactory.h"
#include "lltrans.h"

#include "llradiogroup.h"
#include "llfloaterreg.h"
#include "llerror.h"

#include "llavatarappearancedefines.h"


static const S32 LOCAL_TRACKING_ID_COLUMN = 1;

//static const char CURRENT_IMAGE_NAME[] = "Current Texture";
//static const char WHITE_IMAGE_NAME[] = "Blank Texture";
//static const char NO_IMAGE_NAME[] = "None";



//static
bool get_is_predefined_texture(LLUUID asset_id)
{
    if (asset_id == LLUUID(gSavedSettings.getString("DefaultObjectTexture"))
        || asset_id == LLUUID(gSavedSettings.getString("UIImgWhiteUUID"))
        || asset_id == LLUUID(gSavedSettings.getString("UIImgInvisibleUUID"))
        || asset_id == LLUUID(SCULPT_DEFAULT_TEXTURE))
    {
        return true;
    }
    return false;
}

LLUUID get_copy_free_item_by_asset_id(LLUUID asset_id, bool no_trans_perm)
{
    LLViewerInventoryCategory::cat_array_t cats;
    LLViewerInventoryItem::item_array_t items;
    LLAssetIDMatches asset_id_matches(asset_id);
    gInventory.collectDescendentsIf(LLUUID::null,
        cats,
        items,
        LLInventoryModel::INCLUDE_TRASH,
        asset_id_matches);
    
    LLUUID res;
    if (items.size())
    {
        for (S32 i = 0; i < items.size(); i++)
        {
            LLViewerInventoryItem* itemp = items[i];
            if (itemp)
            {
                LLPermissions item_permissions = itemp->getPermissions();
                if (item_permissions.allowOperationBy(PERM_COPY,
                    gAgent.getID(),
                    gAgent.getGroupID()))
                {
                    bool allow_trans = item_permissions.allowOperationBy(PERM_TRANSFER, gAgent.getID(), gAgent.getGroupID());
                    if (allow_trans != no_trans_perm)
                    {
                        return itemp->getUUID();
                    }
                    res = itemp->getUUID();
                }
            }
        }
    }
    return res;
}

bool get_can_copy_texture(LLUUID asset_id)
{
    // User is allowed to copy a texture if:
    // library asset or default texture,
    // or copy perm asset exists in user's inventory

    return get_is_predefined_texture(asset_id) || get_copy_free_item_by_asset_id(asset_id).notNull();
}

///////////////////////////////////////////////////////////////////////
// LLTextureCtrl

static LLDefaultChildRegistry::Register<LLTextureCtrl> r("texture_picker");

LLTextureCtrl::LLTextureCtrl(const LLTextureCtrl::Params& p)
:	LLUICtrl(p),
	mDragCallback(NULL),
	mDropCallback(NULL),
	mOnCancelCallback(NULL),
	mOnCloseCallback(NULL),
	mOnSelectCallback(NULL),
	mBorderColor( p.border_color() ),
	mAllowNoTexture( FALSE ),
	mAllowLocalTexture( TRUE ),
	mImmediateFilterPermMask( PERM_NONE ),
	mNonImmediateFilterPermMask( PERM_NONE ),
	mCanApplyImmediately( FALSE ),
	mNeedsRawImageData( FALSE ),
	mValid( TRUE ),
	mShowLoadingPlaceholder( TRUE ),
	mOpenTexPreview(false),
	mImageAssetID(p.image_id),
	mDefaultImageAssetID(p.default_image_id),
	mDefaultImageName(p.default_image_name),
	mFallbackImage(p.fallback_image)
{

	LLUUID transparentImage( gSavedSettings.getString( "UIImgTransparentUUID" ) );
	setTransparentImageAssetID( transparentImage );

	// Default of defaults is white image for diff tex
	//
	LLUUID whiteImage( gSavedSettings.getString( "UIImgWhiteUUID" ) );
	setBlankImageAssetID( whiteImage );

	setAllowNoTexture(p.allow_no_texture);
	setCanApplyImmediately(p.can_apply_immediately);
	mCommitOnSelection = !p.no_commit_on_selection;

	LLTextBox::Params params(p.caption_text);
	params.name(p.label);
	params.rect(LLRect( 0, BTN_HEIGHT_SMALL, getRect().getWidth(), 0 ));
	params.initial_value(p.label());
	params.follows.flags(FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_BOTTOM);
	mCaption = LLUICtrlFactory::create<LLTextBox> (params);
	addChild( mCaption );

	S32 image_top = getRect().getHeight();
	S32 image_bottom = BTN_HEIGHT_SMALL;
	S32 image_middle = (image_top + image_bottom) / 2;
	S32 line_height = LLFontGL::getFontSansSerifSmall()->getLineHeight();

	LLTextBox::Params tentative_label_p(p.multiselect_text);
	tentative_label_p.name("Multiple");
	tentative_label_p.rect(LLRect (0, image_middle + line_height / 2, getRect().getWidth(), image_middle - line_height / 2 ));
	tentative_label_p.follows.flags(FOLLOWS_ALL);
	mTentativeLabel = LLUICtrlFactory::create<LLTextBox> (tentative_label_p);

	// It is no longer possible to associate a style with a textbox, so it has to be done in this fashion
	LLStyle::Params style_params;
	style_params.color = LLColor4::white;

	mTentativeLabel->setText(LLTrans::getString("multiple_textures"), style_params);
	mTentativeLabel->setHAlign(LLFontGL::HCENTER);
	addChild( mTentativeLabel );

	LLRect border_rect = getLocalRect();
	border_rect.mBottom += BTN_HEIGHT_SMALL;
	LLViewBorder::Params vbparams(p.border);
	vbparams.name("border");
	vbparams.rect(border_rect);
	mBorder = LLUICtrlFactory::create<LLViewBorder> (vbparams);
	addChild(mBorder);

	mLoadingPlaceholderString = LLTrans::getString("texture_loading");
}

LLTextureCtrl::~LLTextureCtrl()
{
	closeDependentFloater();
}

void LLTextureCtrl::setShowLoadingPlaceholder(BOOL showLoadingPlaceholder)
{
	mShowLoadingPlaceholder = showLoadingPlaceholder;
}

void LLTextureCtrl::setCaption(const std::string& caption)
{
	mCaption->setText( caption );
}

void LLTextureCtrl::setCanApplyImmediately(BOOL b)
{
	mCanApplyImmediately = b; 
	LLFloaterTexturePicker* floaterp = (LLFloaterTexturePicker*)mFloaterHandle.get();
	if( floaterp )
	{
		floaterp->setCanApplyImmediately(b);
	}
}

void LLTextureCtrl::setCanApply(bool can_preview, bool can_apply)
{
	LLFloaterTexturePicker* floaterp = dynamic_cast<LLFloaterTexturePicker*>(mFloaterHandle.get());
	if( floaterp )
	{
		floaterp->setCanApply(can_preview, can_apply);
	}
}

void LLTextureCtrl::setVisible( BOOL visible ) 
{
	if( !visible )
	{
		closeDependentFloater();
	}
	LLUICtrl::setVisible( visible );
}

void LLTextureCtrl::setEnabled( BOOL enabled )
{
	LLFloaterTexturePicker* floaterp = (LLFloaterTexturePicker*)mFloaterHandle.get();
	if( floaterp )
	{
		floaterp->setActive(enabled);
	}
	if( enabled )
	{
		std::string tooltip;
		if (floaterp) tooltip = floaterp->getString("choose_picture");
		setToolTip( tooltip );
	}
	else
	{
		setToolTip( std::string() );
		// *TODO: would be better to keep floater open and show
		// disabled state.
		closeDependentFloater();
	}

	mCaption->setEnabled( enabled );

	LLView::setEnabled( enabled );
}

void LLTextureCtrl::setValid(BOOL valid )
{
	mValid = valid;
	if (!valid)
	{
		LLFloaterTexturePicker* pickerp = (LLFloaterTexturePicker*)mFloaterHandle.get();
		if (pickerp)
		{
			pickerp->setActive(FALSE);
		}
	}
}


// virtual
void LLTextureCtrl::clear()
{
	setImageAssetID(LLUUID::null);
}

void LLTextureCtrl::setLabel(const std::string& label)
{
	mLabel = label;
	mCaption->setText(label);
}

void LLTextureCtrl::showPicker(BOOL take_focus)
{
	// show hourglass cursor when loading inventory window
	// because inventory construction is slooow
	getWindow()->setCursor(UI_CURSOR_WAIT);
	LLFloater* floaterp = mFloaterHandle.get();

	// Show the dialog
	if( floaterp )
	{
		floaterp->openFloater();
	}
	else
	{
		floaterp = new LLFloaterTexturePicker(
			this,
			getImageAssetID(),
			getDefaultImageAssetID(),
			getTransparentImageAssetID(),
			getBlankImageAssetID(),
			getTentative(),
			getAllowNoTexture(),
			mLabel,
			mImmediateFilterPermMask,
			mDnDFilterPermMask,
			mNonImmediateFilterPermMask,
			mCanApplyImmediately,
			mFallbackImage);
		mFloaterHandle = floaterp->getHandle();

		LLFloaterTexturePicker* texture_floaterp = dynamic_cast<LLFloaterTexturePicker*>(floaterp);
		if (texture_floaterp && mOnTextureSelectedCallback)
		{
			texture_floaterp->setTextureSelectedCallback(mOnTextureSelectedCallback);
		}
		if (texture_floaterp && mOnCloseCallback)
		{
			texture_floaterp->setOnFloaterCloseCallback(boost::bind(&LLTextureCtrl::onFloaterClose, this));
		}
		if (texture_floaterp)
		{
			texture_floaterp->setOnFloaterCommitCallback(boost::bind(&LLTextureCtrl::onFloaterCommit, this, _1, _2));
		}
		if (texture_floaterp)
		{
			texture_floaterp->setSetImageAssetIDCallback(boost::bind(&LLTextureCtrl::setImageAssetID, this, _1));
		}
		if (texture_floaterp)
		{
			texture_floaterp->setBakeTextureEnabled(TRUE);
		}

		LLFloater* root_floater = gFloaterView->getParentFloater(this);
		if (root_floater)
			root_floater->addDependentFloater(floaterp);
		floaterp->openFloater();
	}

	LLFloaterTexturePicker* picker_floater = dynamic_cast<LLFloaterTexturePicker*>(floaterp);
	if (picker_floater)
	{
		picker_floater->setLocalTextureEnabled(mAllowLocalTexture);
	}

	if (take_focus)
	{
		floaterp->setFocus(TRUE);
	}
}


void LLTextureCtrl::closeDependentFloater()
{
	LLFloaterTexturePicker* floaterp = (LLFloaterTexturePicker*)mFloaterHandle.get();
	if( floaterp && floaterp->isInVisibleChain())
	{
		floaterp->setOwner(NULL);
		floaterp->setVisible(FALSE);
		floaterp->closeFloater();
	}
}

// Allow us to download textures quickly when floater is shown
class LLTextureFetchDescendentsObserver : public LLInventoryFetchDescendentsObserver
{
public:
	virtual void done()
	{
		// We need to find textures in all folders, so get the main
		// background download going.
		LLInventoryModelBackgroundFetch::instance().start();
		gInventory.removeObserver(this);
		delete this;
	}
};

BOOL LLTextureCtrl::handleHover(S32 x, S32 y, MASK mask)
{
	getWindow()->setCursor(mBorder->parentPointInView(x,y) ? UI_CURSOR_HAND : UI_CURSOR_ARROW);
	return TRUE;
}


BOOL LLTextureCtrl::handleMouseDown(S32 x, S32 y, MASK mask)
{
	BOOL handled = LLUICtrl::handleMouseDown( x, y , mask );

	if (!handled && mBorder->parentPointInView(x, y))
	{
		if (!mOpenTexPreview)
		{
			showPicker(FALSE);
			//grab textures first...
			LLInventoryModelBackgroundFetch::instance().start(gInventory.findCategoryUUIDForType(LLFolderType::FT_TEXTURE));
			//...then start full inventory fetch.
			LLInventoryModelBackgroundFetch::instance().start();
			handled = TRUE;
		}
		else
		{
			if (getImageAssetID().notNull())
			{
				LLPreviewTexture* preview_texture = LLFloaterReg::showTypedInstance<LLPreviewTexture>("preview_texture", getValue());
				if (preview_texture && !preview_texture->isDependent())
				{
					LLFloater* root_floater = gFloaterView->getParentFloater(this);
					if (root_floater)
					{
						root_floater->addDependentFloater(preview_texture);
						preview_texture->hideCtrlButtons();
					}
				}
			}
		}
	}

	return handled;
}

void LLTextureCtrl::onFloaterClose()
{
	LLFloaterTexturePicker* floaterp = (LLFloaterTexturePicker*)mFloaterHandle.get();

	if (floaterp)
	{
		if (mOnCloseCallback)
		{
			mOnCloseCallback(this,LLSD());
		}
		floaterp->setOwner(NULL);
	}

	mFloaterHandle.markDead();
}

void LLTextureCtrl::onFloaterCommit(ETexturePickOp op, LLUUID id)
{
	LLFloaterTexturePicker* floaterp = (LLFloaterTexturePicker*)mFloaterHandle.get();

	if( floaterp && getEnabled())
	{
		if (op == TEXTURE_CANCEL)
			mViewModel->resetDirty();
		// If the "no_commit_on_selection" parameter is set
		// we get dirty only when user presses OK in the picker
		// (i.e. op == TEXTURE_SELECT) or texture changes via DnD.
		else if (mCommitOnSelection || op == TEXTURE_SELECT)
			mViewModel->setDirty(); // *TODO: shouldn't we be using setValue() here?
			
		if(floaterp->isDirty() || id.notNull()) // mModelView->setDirty does not work.
		{
			setTentative( FALSE );

			if (id.notNull())
			{
				mImageItemID = id;
				mImageAssetID = id;
			}
			else
			{
			mImageItemID = floaterp->findItemID(floaterp->getAssetID(), FALSE);
			LL_DEBUGS() << "mImageItemID: " << mImageItemID << LL_ENDL;
			mImageAssetID = floaterp->getAssetID();
			LL_DEBUGS() << "mImageAssetID: " << mImageAssetID << LL_ENDL;
			}

			if (op == TEXTURE_SELECT && mOnSelectCallback)
			{
				mOnSelectCallback( this, LLSD() );
			}
			else if (op == TEXTURE_CANCEL && mOnCancelCallback)
			{
				mOnCancelCallback( this, LLSD() );
			}
			else
			{
				// If the "no_commit_on_selection" parameter is set
				// we commit only when user presses OK in the picker
				// (i.e. op == TEXTURE_SELECT) or texture changes via DnD.
				if (mCommitOnSelection || op == TEXTURE_SELECT)
					onCommit();
			}
		}
	}
}

void LLTextureCtrl::setOnTextureSelectedCallback(texture_selected_callback cb)
{
	mOnTextureSelectedCallback = cb;
	LLFloaterTexturePicker* floaterp = dynamic_cast<LLFloaterTexturePicker*>(mFloaterHandle.get());
	if (floaterp)
	{
		floaterp->setTextureSelectedCallback(cb);
	}
}

void	LLTextureCtrl::setImageAssetName(const std::string& name)
{
	LLPointer<LLUIImage> imagep = LLUI::getUIImage(name);
	if(imagep)
	{
		LLViewerFetchedTexture* pTexture = dynamic_cast<LLViewerFetchedTexture*>(imagep->getImage().get());
		if(pTexture)
		{
			LLUUID id = pTexture->getID();
			setImageAssetID(id);
		}
	}
}

void LLTextureCtrl::setImageAssetID( const LLUUID& asset_id )
{
	if( mImageAssetID != asset_id )
	{
		mImageItemID.setNull();
		mImageAssetID = asset_id;
		LLFloaterTexturePicker* floaterp = (LLFloaterTexturePicker*)mFloaterHandle.get();
		if( floaterp && getEnabled() )
		{
			floaterp->setImageID( asset_id );
			floaterp->resetDirty();
		}
	}
}

void LLTextureCtrl::setBakeTextureEnabled(BOOL enabled)
{
	LLFloaterTexturePicker* floaterp = (LLFloaterTexturePicker*)mFloaterHandle.get();
	if (floaterp)
	{
		floaterp->setBakeTextureEnabled(enabled);
	}
}

BOOL LLTextureCtrl::handleDragAndDrop(S32 x, S32 y, MASK mask,
					  BOOL drop, EDragAndDropType cargo_type, void *cargo_data,
					  EAcceptance *accept,
					  std::string& tooltip_msg)
{
	BOOL handled = FALSE;

	// this downcast may be invalid - but if the second test below
	// returns true, then the cast was valid, and we can perform
	// the third test without problems.
	LLInventoryItem* item = (LLInventoryItem*)cargo_data; 
	bool is_mesh = cargo_type == DAD_MESH;

	if (getEnabled() &&
		((cargo_type == DAD_TEXTURE) || is_mesh) &&
		 allowDrop(item))
	{
		if (drop)
		{
			if(doDrop(item))
			{
				if (!mCommitOnSelection)
					mViewModel->setDirty();

				// This removes the 'Multiple' overlay, since
				// there is now only one texture selected.
				setTentative( FALSE ); 
				onCommit();
			}
		}

		*accept = ACCEPT_YES_SINGLE;
	}
	else
	{
		*accept = ACCEPT_NO;
	}

	handled = TRUE;
	LL_DEBUGS("UserInput") << "dragAndDrop handled by LLTextureCtrl " << getName() << LL_ENDL;

	return handled;
}

void LLTextureCtrl::draw()
{
	mBorder->setKeyboardFocusHighlight(hasFocus());

	if (!mValid)
	{
		mTexturep = NULL;
	}
	else if (!mImageAssetID.isNull())
	{
		LLPointer<LLViewerFetchedTexture> texture = NULL;

		if (LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(mImageAssetID))
		{
			LLViewerObject* obj = LLSelectMgr::getInstance()->getSelection()->getFirstObject();
			if (obj)
			{
				LLViewerTexture* viewerTexture = obj->getBakedTextureForMagicId(mImageAssetID);
				texture = viewerTexture ? dynamic_cast<LLViewerFetchedTexture*>(viewerTexture) : NULL;
			}
			
		}

		if (texture.isNull())
		{
			texture = LLViewerTextureManager::getFetchedTexture(mImageAssetID, FTT_DEFAULT, MIPMAP_YES, LLGLTexture::BOOST_NONE, LLViewerTexture::LOD_TEXTURE);
		}
		
		texture->setBoostLevel(LLGLTexture::BOOST_PREVIEW);
		texture->forceToSaveRawImage(0) ;

		mTexturep = texture;
	}
	else//mImageAssetID == LLUUID::null
	{
		mTexturep = NULL;
	}
	
	// Border
	LLRect border( 0, getRect().getHeight(), getRect().getWidth(), BTN_HEIGHT_SMALL );
	gl_rect_2d( border, mBorderColor.get(), FALSE );

	// Interior
	LLRect interior = border;
	interior.stretch( -1 ); 

	// If we're in a focused floater, don't apply the floater's alpha to the texture (STORM-677).
	const F32 alpha = getTransparencyType() == TT_ACTIVE ? 1.0f : getCurrentTransparency();
	if( mTexturep )
	{
		if( mTexturep->getComponents() == 4 )
		{
			gl_rect_2d_checkerboard( interior, alpha );
		}
		
		gl_draw_scaled_image( interior.mLeft, interior.mBottom, interior.getWidth(), interior.getHeight(), mTexturep, UI_VERTEX_COLOR % alpha);
		mTexturep->addTextureStats( (F32)(interior.getWidth() * interior.getHeight()) );
	}
	else if (!mFallbackImage.isNull())
	{
		mFallbackImage->draw(interior, UI_VERTEX_COLOR % alpha);
	}
	else
	{
		gl_rect_2d( interior, LLColor4::grey % alpha, TRUE );

		// Draw X
		gl_draw_x( interior, LLColor4::black );
	}

	mTentativeLabel->setVisible( getTentative() );

	// Show "Loading..." string on the top left corner while this texture is loading.
	// Using the discard level, do not show the string if the texture is almost but not 
	// fully loaded.
	if (mTexturep.notNull() &&
		(!mTexturep->isFullyLoaded()) &&
		(mShowLoadingPlaceholder == TRUE))
	{
		U32 v_offset = 25;
		LLFontGL* font = LLFontGL::getFontSansSerif();

		// Don't show as loaded if the texture is almost fully loaded (i.e. discard1) unless god
		if ((mTexturep->getDiscardLevel() > 1) || gAgent.isGodlike())
		{
			font->renderUTF8(
				mLoadingPlaceholderString, 
				0,
				llfloor(interior.mLeft+3), 
				llfloor(interior.mTop-v_offset),
				LLColor4::white,
				LLFontGL::LEFT,
				LLFontGL::BASELINE,
				LLFontGL::DROP_SHADOW);
		}

		// Optionally show more detailed information.
		if (ALControlCache::DebugAvatarRezTime)
		{
			LLFontGL* font = LLFontGL::getFontSansSerif();
			std::string tdesc;
			// Show what % the texture has loaded (0 to 100%, 100 is highest), and what level of detail (5 to 0, 0 is best).

			v_offset += 12;
			tdesc = llformat("  PK  : %d%%", U32(mTexturep->getDownloadProgress()*100.0));
			font->renderUTF8(tdesc, 0, llfloor(interior.mLeft+3), llfloor(interior.mTop-v_offset),
							 LLColor4::white, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::DROP_SHADOW);

			v_offset += 12;
			tdesc = llformat("  LVL: %d", mTexturep->getDiscardLevel());
			font->renderUTF8(tdesc, 0, llfloor(interior.mLeft+3), llfloor(interior.mTop-v_offset),
							 LLColor4::white, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::DROP_SHADOW);

			v_offset += 12;
			tdesc = llformat("  ID  : %s...", (mImageAssetID.asString().substr(0,7)).c_str());
			font->renderUTF8(tdesc, 0, llfloor(interior.mLeft+3), llfloor(interior.mTop-v_offset),
							 LLColor4::white, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::DROP_SHADOW);
		}
	}

	LLUICtrl::draw();
}

BOOL LLTextureCtrl::allowDrop(LLInventoryItem* item)
{
	BOOL copy = item->getPermissions().allowCopyBy(gAgent.getID());
	BOOL mod = item->getPermissions().allowModifyBy(gAgent.getID());
	BOOL xfer = item->getPermissions().allowOperationBy(PERM_TRANSFER,
														gAgent.getID());

	PermissionMask item_perm_mask = 0;
	if (copy) item_perm_mask |= PERM_COPY;
	if (mod)  item_perm_mask |= PERM_MODIFY;
	if (xfer) item_perm_mask |= PERM_TRANSFER;
	
//	PermissionMask filter_perm_mask = mCanApplyImmediately ?			commented out due to no-copy texture loss.
//			mImmediateFilterPermMask : mNonImmediateFilterPermMask;
	PermissionMask filter_perm_mask = mImmediateFilterPermMask;
	if ( (item_perm_mask & filter_perm_mask) == filter_perm_mask )
	{
		if(mDragCallback)
		{
			return mDragCallback(this, item);
		}
		else
		{
			return TRUE;
		}
	}
	else
	{
		return FALSE;
	}
}

BOOL LLTextureCtrl::doDrop(LLInventoryItem* item)
{
	// call the callback if it exists.
	if(mDropCallback)
	{
		// if it returns TRUE, we return TRUE, and therefore the
		// commit is called above.
		return mDropCallback(this, item);
	}

	// no callback installed, so just set the image ids and carry on.
	setImageAssetID( item->getAssetUUID() );
	mImageItemID = item->getUUID();
	return TRUE;
}

BOOL LLTextureCtrl::handleUnicodeCharHere(llwchar uni_char)
{
	if( ' ' == uni_char )
	{
		showPicker(TRUE);
		return TRUE;
	}
	return LLUICtrl::handleUnicodeCharHere(uni_char);
}

void LLTextureCtrl::setValue( const LLSD& value )
{
	setImageAssetID(value.asUUID());
}

LLSD LLTextureCtrl::getValue() const
{
	return LLSD(getImageAssetID());
}





