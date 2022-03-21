/** 
 * @file llviewertexlayer.h
 * @brief Viewer Texture layer classes. Used for avatars.
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
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

#ifndef LL_VIEWER_TEXLAYER_H
#define LL_VIEWER_TEXLAYER_H

#include "lldynamictexture.h"
#include "llextendedstatus.h"
#include "lltexlayer.h"

class LLVOAvatarSelf;
class LLViewerTexLayerSetBuffer;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LLViewerTexLayerSet
//
// An ordered set of texture layers that gets composited into a single texture.
// Only exists for llavatarappearanceself.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
class LLViewerTexLayerSet final : public LLTexLayerSet
{
public:
	LLViewerTexLayerSet(LLAvatarAppearance* const appearance);
	virtual ~LLViewerTexLayerSet();

	/*virtual*/void				requestUpdate() override;
	void						requestUpload();
	void						cancelUpload();
	BOOL						isLocalTextureDataAvailable() const;
	BOOL						isLocalTextureDataFinal() const;
	void						updateComposite();
	/*virtual*/void				createComposite() override;
	void						setUpdatesEnabled(BOOL b);
	BOOL						getUpdatesEnabled()	const 	{ return mUpdatesEnabled; }

	LLVOAvatarSelf*				getAvatar();
	const LLVOAvatarSelf*		getAvatar()	const;
	LLViewerTexLayerSetBuffer*	getViewerComposite();
	const LLViewerTexLayerSetBuffer*	getViewerComposite() const;

private:
	BOOL						mUpdatesEnabled;

};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LLViewerTexLayerSetBuffer
//
// The composite image that a LLViewerTexLayerSet writes to.  Each LLViewerTexLayerSet has one.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
class LLViewerTexLayerSetBuffer final : public LLTexLayerSetBuffer, public LLViewerDynamicTexture
{
	LOG_CLASS(LLViewerTexLayerSetBuffer);

public:
	LLViewerTexLayerSetBuffer(LLTexLayerSet* const owner, S32 width, S32 height);
	virtual ~LLViewerTexLayerSetBuffer();

public:
	/*virtual*/ S8          getType() const override;
	BOOL					isInitialized(void) const;
	static void				dumpTotalByteCount();
	const std::string		dumpTextureInfo() const;
	void 			restoreGLTexture() override;
	void 			destroyGLTexture() override;
private:
	LLViewerTexLayerSet*	getViewerTexLayerSet() 
		{ return dynamic_cast<LLViewerTexLayerSet*> (mTexLayerSet); }
	const LLViewerTexLayerSet*	getViewerTexLayerSet() const
		{ return dynamic_cast<const LLViewerTexLayerSet*> (mTexLayerSet); }
	static S32				sGLByteCount;

	//--------------------------------------------------------------------
	// Tex Layer Render
	//--------------------------------------------------------------------
	void			preRenderTexLayerSet() override;
	void			midRenderTexLayerSet(BOOL success, LLRenderTarget* bound_target) override;
	void			postRenderTexLayerSet(BOOL success) override;
	S32				getCompositeOriginX() const override { return getOriginX(); }
	S32				getCompositeOriginY() const override { return getOriginY(); }
	S32				getCompositeWidth() const override { return getFullWidth(); }
	S32				getCompositeHeight() const override { return getFullHeight(); }

	//--------------------------------------------------------------------
	// Dynamic Texture Interface
	//--------------------------------------------------------------------
public:
	/*virtual*/ BOOL		needsRender() override;
protected:
	// Pass these along for tex layer rendering.
	void			preRender(BOOL clear_depth) override { preRenderTexLayerSet(); }
	void			postRender(BOOL success) override { postRenderTexLayerSet(success); }
	BOOL			render() override { return renderTexLayerSet(mBoundTarget); }


	//--------------------------------------------------------------------
	// Uploads
	//--------------------------------------------------------------------
public:
	void					requestUpload();
	void					cancelUpload();
	BOOL					uploadNeeded() const; 			// We need to upload a new texture
	BOOL					uploadInProgress() const; 		// We have started uploading a new texture and are awaiting the result
	BOOL					uploadPending() const; 			// We are expecting a new texture to be uploaded at some point
	static void				onTextureUploadComplete(const LLUUID& uuid,
													void* userdata,
													S32 result, LLExtStat ext_status);
protected:
	BOOL					isReadyToUpload() const;
	void					doUpload(LLRenderTarget* bound_target); 					// Does a read back and upload.
	void					conditionalRestartUploadTimer();
private:
	BOOL					mNeedsUpload; 					// Whether we need to send our baked textures to the server
	U32						mNumLowresUploads; 				// Number of times we've sent a lowres version of our baked textures to the server
	BOOL					mUploadPending; 				// Whether we have received back the new baked textures
	LLUUID					mUploadID; 						// The current upload process (null if none).
	LLFrameTimer    		mNeedsUploadTimer; 				// Tracks time since upload was requested and performed.
	S32						mUploadFailCount;				// Number of consecutive upload failures
	LLFrameTimer    		mUploadRetryTimer; 				// Tracks time since last upload failure.

	//--------------------------------------------------------------------
	// Updates
	//--------------------------------------------------------------------
public:
	void					requestUpdate();
	BOOL					requestUpdateImmediate();
protected:
	BOOL					isReadyToUpdate() const;
	void					doUpdate();
	void					restartUpdateTimer();
private:
	BOOL					mNeedsUpdate; 					// Whether we need to locally update our baked textures
	U32						mNumLowresUpdates; 				// Number of times we've locally updated with lowres version of our baked textures
	LLFrameTimer    		mNeedsUpdateTimer; 				// Tracks time since update was requested and performed.
};


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// LLBakedUploadData
//
// Used by LLTexLayerSetBuffer for a callback.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
struct LLBakedUploadData
{
	LLBakedUploadData(const LLVOAvatarSelf* avatar,
					  LLViewerTexLayerSet* layerset, 
					  const LLUUID& id,
					  bool highest_res);
	~LLBakedUploadData() = default;
	const LLUUID				mID;
	const LLVOAvatarSelf*		mAvatar; // note: backlink only; don't LLPointer 
	LLViewerTexLayerSet*		mTexLayerSet;
   	const U64					mStartTime;	// for measuring baked texture upload time
   	const bool					mIsHighestRes; // whether this is a "final" bake, or intermediate low res
};
#endif  // LL_VIEWER_TEXLAYER_H

