/**
 * @file lltextureview.cpp
 * @brief LLTextureView class implementation
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012-2013, Linden Research, Inc.
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

#include <set>

#include "lltextureview.h"

#include "llrect.h"
#include "llerror.h"
#include "lllfsthread.h"
#include "llui.h"
#include "llimageworker.h"
#include "llrender.h"

#include "lltooltip.h"
#include "llappviewer.h"
#include "llmeshrepository.h"
#include "llselectmgr.h"
#include "llviewertexlayer.h"
#include "lltexturecache.h"
#include "lltexturefetch.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewertexture.h"
#include "llviewertexturelist.h"
#include "llviewerthrottle.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "llvovolume.h"
#include "llviewerstats.h"
#include "llworld.h"

// For avatar texture view
#include "llvoavatarself.h"
#include "lltexlayer.h"

LLTextureView *gTextureView = NULL;

#define HIGH_PRIORITY 100000000.f

//static
std::set<LLViewerFetchedTexture*> LLTextureView::sDebugImages;

////////////////////////////////////////////////////////////////////////////

// "DecodePri(Fetch)" and "pk/max" used to label columns that the data row no
// longer renders; trimmed out so the header matches what's actually shown.
static std::string title_string1("Tex UUID Area  DDis(Req)  [download]");
static std::string title_string2("State");
static std::string title_string3("Pkt Bnd");
static std::string title_string4("  W x H (Dis) Mem");

// Column layout — recomputed each frame in LLTextureView::draw() based on the
// current monospace font's metrics so widening the font doesn't make headers
// overlap each other or the per-row pip / progress-bar columns.
static S32 title_x1 = 0;
static S32 title_x2 = 460;
static S32 title_x3 = title_x2 + 40;
static S32 title_x4 = title_x3 + 46;
static S32 char_w = 8;
static S32 bar_left = 260;
static S32 bar_width = 100;
// Right-edge (in LLGLTexMemBar local coords) of the widest status line drawn by
// that bar; published each frame in LLGLTexMemBar::draw() and read on the next
// frame by required_view_width() so the parent view grows to fit.
static S32 max_status_w = 0;

static void update_layout()
{
    const LLFontGL* font = LLFontGL::getFontMonospace();
    char_w = llmax(1, font->getWidth(std::string("M")));

    // Align the progress bar with "[download]" in the column-1 header so the
    // bar sits where its label says.
    const size_t dl_pos = title_string1.find("[download]");
    if (dl_pos != std::string::npos)
    {
        bar_left = font->getWidth(title_string1.substr(0, dl_pos));
        bar_width = font->getWidth(title_string1.substr(dl_pos));
    }
    else
    {
        bar_left = char_w * 26;
        bar_width = char_w * 10;
    }

    const S32 title1_w = font->getWidth(title_string1);
    // Two 6px pips with 14px spacing need ~50px regardless of font.
    const S32 pip_column_w = 50;

    title_x1 = 0;
    title_x2 = llmax(bar_left + bar_width + char_w * 2, title1_w + char_w);
    title_x3 = title_x2 + font->getWidth(title_string2) + char_w * 2;
    title_x4 = title_x3 + llmax(font->getWidth(title_string3), pip_column_w) + char_w * 2;
}

static S32 required_view_width()
{
    const LLFontGL* font = LLFontGL::getFontMonospace();
    // Col-4 data row is "%3dx%3d (%2d) %7d" — up to 21 chars; pick the wider of
    // the header text and that data extent.
    const S32 col4_w = llmax(font->getWidth(title_string4), char_w * 21);
    const S32 columns_w = title_x4 + col4_w;
    // The non-header status lines drawn by LLGLTexMemBar (e.g. the "Est. Free…
    // Cache:" line and "Net Tot Tex…") run wider than the column row.
    const S32 content_w = llmax(columns_w, max_status_w);
    // arrange() pads with left=10 / right=2 around each child (12 total).
    return content_w + char_w + 12;
}

////////////////////////////////////////////////////////////////////////////

class LLTextureBar : public LLView
{
public:
    LLPointer<LLViewerFetchedTexture> mImagep;
    S32 mHilite;

public:
    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Mandatory<LLTextureView*> texture_view;
        Params()
        :   texture_view("texture_view")
        {
            changeDefault(mouse_opaque, false);
        }
    };
    LLTextureBar(const Params& p)
    :   LLView(p),
        mHilite(0),
        mTextureView(p.texture_view)
    {}

    virtual void draw();
    virtual bool handleMouseDown(S32 x, S32 y, MASK mask);
    virtual LLRect getRequiredRect();   // Return the height of this object, given the set options.

// Used for sorting
    struct sort
    {
        bool operator()(const LLView* i1, const LLView* i2)
        {
            LLTextureBar* bar1p = (LLTextureBar*)i1;
            LLTextureBar* bar2p = (LLTextureBar*)i2;
            LLViewerFetchedTexture *i1p = bar1p->mImagep;
            LLViewerFetchedTexture *i2p = bar2p->mImagep;
            F32 pri1 = i1p->getMaxVirtualSize();
            F32 pri2 = i2p->getMaxVirtualSize();
            if (pri1 > pri2)
                return true;
            else if (pri2 > pri1)
                return false;
            else
                return i1p->getID() < i2p->getID();
        }
    };

    struct sort_fetch
    {
        bool operator()(const LLView* i1, const LLView* i2)
        {
            LLTextureBar* bar1p = (LLTextureBar*)i1;
            LLTextureBar* bar2p = (LLTextureBar*)i2;
            LLViewerFetchedTexture *i1p = bar1p->mImagep;
            LLViewerFetchedTexture *i2p = bar2p->mImagep;
            U32 pri1 = i1p->getFetchPriority() ;
            U32 pri2 = i2p->getFetchPriority() ;
            if (pri1 > pri2)
                return true;
            else if (pri2 > pri1)
                return false;
            else
                return i1p->getID() < i2p->getID();
        }
    };
private:
    LLTextureView* mTextureView;
};

void LLTextureBar::draw()
{
    if (!mImagep)
    {
        return;
    }

    LLColor4 color;
    if (mImagep->getID() == LLAppViewer::getTextureFetch()->mDebugID)
    {
        color = LLColor4::cyan2;
    }
    else if (mHilite)
    {
        S32 idx = llclamp(mHilite,1,3);
        if (idx==1) color = LLColor4::orange;
        else if (idx==2) color = LLColor4::yellow;
        else color = LLColor4::pink2;
    }
    else if (mImagep->mDontDiscard)
    {
        color = LLColor4::green4;
    }
    else if (mImagep->getMaxVirtualSize() <= 0.0f)
    {
        color = LLColor4::grey; color[VALPHA] = .7f;
    }
    else
    {
        color = LLColor4::white; color[VALPHA] = .7f;
    }

    // We need to draw:
    // The texture UUID or name
    // The progress bar for the texture, highlighted if it's being download
    // Various numerical stats.
    std::string tex_str;
    S32 left, right;
    S32 top = 0;
    S32 bottom = top + 6;
    LLColor4 clr;

    LLGLSUIDefault gls_ui;

    // Name, pixel_area, requested pixel area, decode priority
    std::string uuid_str;
    mImagep->mID.toString(uuid_str);
    uuid_str = uuid_str.substr(0,7);

    tex_str = llformat("%s %7.0f %d(%d)",
        uuid_str.c_str(),
        mImagep->mMaxVirtualSize,
        mImagep->mDesiredDiscardLevel,
        mImagep->mRequestedDiscardLevel);


    LLFontGL::getFontMonospace()->renderUTF8(tex_str, 0, title_x1, getRect().getHeight(),
                                     color, LLFontGL::LEFT, LLFontGL::TOP);

    // State
    // Hack: mirrored from lltexturefetch.cpp
    struct { const std::string desc; LLColor4 color; } fetch_state_desc[] = {
        { "---", LLColor4::red },   // INVALID
        { "INI", LLColor4::white }, // INIT
        { "DSK", LLColor4::cyan },  // LOAD_FROM_TEXTURE_CACHE
        { "DSK", LLColor4::blue },  // CACHE_POST
        { "NET", LLColor4::green }, // LOAD_FROM_NETWORK
        { "HTW", LLColor4::green }, // WAIT_HTTP_RESOURCE
        { "HTW", LLColor4::green }, // WAIT_HTTP_RESOURCE2
        { "REQ", LLColor4::yellow },// SEND_HTTP_REQ
        { "HTP", LLColor4::green }, // WAIT_HTTP_REQ
        { "DEC", LLColor4::yellow },// DECODE_IMAGE
        { "DEU", LLColor4::green }, // DECODE_IMAGE_UPDATE
        { "WRT", LLColor4::purple },// WRITE_TO_CACHE
        { "WWT", LLColor4::orange },// WAIT_ON_WRITE
        { "END", LLColor4::red },   // DONE
#define LAST_STATE 14
        { "CRE", LLColor4::magenta }, // LAST_STATE+1
        { "FUL", LLColor4::green }, // LAST_STATE+2
        { "BAD", LLColor4::red }, // LAST_STATE+3
        { "MIS", LLColor4::red }, // LAST_STATE+4
        { "---", LLColor4::white }, // LAST_STATE+5
    };
    const S32 fetch_state_desc_size = (S32)LL_ARRAY_SIZE(fetch_state_desc);
    S32 state =
        mImagep->mNeedsCreateTexture ? LAST_STATE+1 :
        mImagep->mFullyLoaded ? LAST_STATE+2 :
        mImagep->mMinDiscardLevel > 0 ? LAST_STATE+3 :
        mImagep->mIsMissingAsset ? LAST_STATE+4 :
        !mImagep->mIsFetching ? LAST_STATE+5 :
        mImagep->mFetchState;
    state = llclamp(state,0,fetch_state_desc_size-1);

    LLFontGL::getFontMonospace()->renderUTF8(fetch_state_desc[state].desc, 0, title_x2, getRect().getHeight(),
                                     fetch_state_desc[state].color,
                                     LLFontGL::LEFT, LLFontGL::TOP);
    gGL.getTextureSlot(0)->unbind();

    // Draw the progress bar (column / width set in update_layout()).
    left = bar_left;
    right = left + bar_width;

    gGL.color4f(0.f, 0.f, 0.f, 0.75f);
    gl_rect_2d(left, top, right, bottom);

    // Clamp: mDownloadProgress can temporarily exceed 1.0 mid-fetch; an unclamped
    // value paints the blue bar far past its column.
    F32 data_progress = llclamp(mImagep->mDownloadProgress, 0.f, 1.f);

    if (data_progress > 0.0f)
    {
        // Downloaded bytes
        right = left + llfloor(data_progress * (F32)bar_width);
        if (right > left)
        {
            gGL.color4f(0.f, 0.f, 1.f, 0.75f);
            gl_rect_2d(left, top, right, bottom);
        }
    }

    S32 pip_width = 6;
    S32 pip_space = 14;
    S32 pip_x = title_x3 + pip_space/2;

    // Draw the packet pip
    const F32 pip_max_time = 5.f;
    F32 last_event = mImagep->mLastPacketTimer.getElapsedTimeF32();
    if (last_event < pip_max_time)
    {
        clr = LLColor4::white;
    }
    else
    {
        last_event = mImagep->mRequestDeltaTime;
        if (last_event < pip_max_time)
        {
            clr = LLColor4::green;
        }
        else
        {
            last_event = mImagep->mFetchDeltaTime;
            if (last_event < pip_max_time)
            {
                clr = LLColor4::yellow;
            }
        }
    }
    if (last_event < pip_max_time)
    {
        clr.setAlpha(1.f - last_event/pip_max_time);
        gGL.color4fv(clr.mV);
        gl_rect_2d(pip_x, top, pip_x + pip_width, bottom);
    }
    pip_x += pip_width + pip_space;

    // we don't want to show bind/resident pips for textures using the default texture
    if (mImagep->hasGLTexture())
    {
        // Draw the bound pip
        last_event = mImagep->getTimePassedSinceLastBound();
        if (last_event < 1.f)
        {
            clr = mImagep->getMissed() ? LLColor4::red : LLColor4::magenta1;
            clr.setAlpha(1.f - last_event);
            gGL.color4fv(clr.mV);
            gl_rect_2d(pip_x, top, pip_x + pip_width, bottom);
        }
    }
    pip_x += pip_width + pip_space;


    {
        LLGLSUIDefault gls_ui;
        // draw the image size at the end
        {
            std::string num_str = llformat("%3dx%3d (%2d) %7d", mImagep->getWidth(), mImagep->getHeight(),
                mImagep->getDiscardLevel(), mImagep->hasGLTexture() ? mImagep->getTextureMemory().value() : 0);
            LLFontGL::getFontMonospace()->renderUTF8(num_str, 0, title_x4, getRect().getHeight(), color,
                                            LLFontGL::LEFT, LLFontGL::TOP);
        }
    }

}

bool LLTextureBar::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if ((mask & (MASK_CONTROL|MASK_SHIFT|MASK_ALT)) == MASK_ALT)
    {
        LLAppViewer::getTextureFetch()->mDebugID = mImagep->getID();
        return true;
    }
    return LLView::handleMouseDown(x,y,mask);
}

LLRect LLTextureBar::getRequiredRect()
{
    LLRect rect;
    // One line of text plus the 6px progress bar at the bottom; keep a small floor
    // so tiny fonts still leave room for the pip drawing at y=0..6.
    rect.mTop = llmax(LLFontGL::getFontMonospace()->getLineHeight(), 8);
    return rect;
}

////////////////////////////////////////////////////////////////////////////

class LLAvatarTexBar : public LLView
{
public:
    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Mandatory<LLTextureView*>   texture_view;
        Params()
        :   texture_view("texture_view")
        {
            S32 line_height = LLFontGL::getFontMonospace()->getLineHeight();
            changeDefault(rect, LLRect(0,0,100,line_height * 4));
        }
    };

    LLAvatarTexBar(const Params& p)
    :   LLView(p),
        mTextureView(p.texture_view)
    {}

    virtual void draw();
    virtual bool handleMouseDown(S32 x, S32 y, MASK mask);
    virtual LLRect getRequiredRect();   // Return the height of this object, given the set options.

private:
    LLTextureView* mTextureView;
};

void LLAvatarTexBar::draw()
{
    if (!gSavedSettings.getBOOL("DebugAvatarRezTime")) return;

    LLVOAvatarSelf* avatarp = gAgentAvatarp;
    if (!avatarp) return;

    const S32 line_height = LLFontGL::getFontMonospace()->getLineHeight();
    const S32 v_offset = 0;
    const S32 l_offset = 3;

    //----------------------------------------------------------------------------
    LLGLSUIDefault gls_ui;
    LLColor4 color;

    U32 line_num = 1;
    for (LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::BakedTextures::const_iterator baked_iter = LLAvatarAppearance::getDictionary()->getBakedTextures().begin();
         baked_iter != LLAvatarAppearance::getDictionary()->getBakedTextures().end();
         ++baked_iter)
    {
        const LLAvatarAppearanceDefines::EBakedTextureIndex baked_index = baked_iter->first;
        const LLViewerTexLayerSet *layerset = avatarp->debugGetLayerSet(baked_index);
        if (!layerset) continue;
        const LLViewerTexLayerSetBuffer *layerset_buffer = layerset->getViewerComposite();
        if (!layerset_buffer) continue;

        LLColor4 text_color = LLColor4::white;

        std::string text = layerset_buffer->dumpTextureInfo();
        LLFontGL::getFontMonospace()->renderUTF8(text, 0, l_offset, v_offset + line_height*line_num,
                                                 text_color, LLFontGL::LEFT, LLFontGL::TOP); //, LLFontGL::BOLD, LLFontGL::DROP_SHADOW_SOFT);
        line_num++;
    }
    const U32 texture_timeout = gSavedSettings.getU32("AvatarBakedTextureUploadTimeout");
    const U32 override_tex_discard_level = gSavedSettings.getU32("TextureDiscardLevel");

    LLColor4 header_color(1.f, 1.f, 1.f, 0.9f);

    const std::string texture_timeout_str = texture_timeout ? llformat("%d", texture_timeout) : "Disabled";
    const std::string override_tex_discard_level_str = override_tex_discard_level ? llformat("%d",override_tex_discard_level) : "Disabled";
    std::string header_text = llformat("[ Timeout('AvatarBakedTextureUploadTimeout'):%s ] [ LOD_Override('TextureDiscardLevel'):%s ]", texture_timeout_str.c_str(), override_tex_discard_level_str.c_str());
    LLFontGL::getFontMonospace()->renderUTF8(header_text, 0, l_offset, v_offset + line_height*line_num,
                                             header_color, LLFontGL::LEFT, LLFontGL::TOP); //, LLFontGL::BOLD, LLFontGL::DROP_SHADOW_SOFT);
    line_num++;
    std::string section_text = "Avatar Textures Information:";
    LLFontGL::getFontMonospace()->renderUTF8(section_text, 0, 0, v_offset + line_height*line_num,
                                             header_color, LLFontGL::LEFT, LLFontGL::TOP, LLFontGL::BOLD, LLFontGL::DROP_SHADOW_SOFT);
}

bool LLAvatarTexBar::handleMouseDown(S32 x, S32 y, MASK mask)
{
    return false;
}

LLRect LLAvatarTexBar::getRequiredRect()
{
    LLRect rect;
    if (!gSavedSettings.getBOOL("DebugAvatarRezTime")) return rect;

    LLVOAvatarSelf* avatarp = gAgentAvatarp;
    if (!avatarp) return rect;

    // Count exactly what draw() will render: one line per baked layerset that has
    // a viewer composite buffer, plus the two trailing header/section lines.
    S32 line_count = 0;
    for (auto baked_iter = LLAvatarAppearance::getDictionary()->getBakedTextures().begin();
         baked_iter != LLAvatarAppearance::getDictionary()->getBakedTextures().end();
         ++baked_iter)
    {
        const LLViewerTexLayerSet* layerset = avatarp->debugGetLayerSet(baked_iter->first);
        if (!layerset) continue;
        if (!layerset->getViewerComposite()) continue;
        ++line_count;
    }
    line_count += 2; // header_text + section_text

    rect.mTop = LLFontGL::getFontMonospace()->getLineHeight() * line_count;
    return rect;
}

////////////////////////////////////////////////////////////////////////////

class LLGLTexMemBar : public LLView
{
public:
    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        Mandatory<LLTextureView*>   texture_view;
        Params()
        :   texture_view("texture_view")
        {
            S32 line_height = LLFontGL::getFontMonospace()->getLineHeight();
            changeDefault(rect, LLRect(0,0,0,line_height * 8));
        }
    };

    LLGLTexMemBar(const Params& p)
    :   LLView(p),
        mTextureView(p.texture_view)
    {}

    virtual void draw();
    virtual bool handleMouseDown(S32 x, S32 y, MASK mask);
    virtual LLRect getRequiredRect();   // Return the height of this object, given the set options.

private:
    LLTextureView* mTextureView;
};

void LLGLTexMemBar::draw()
{
    F32 discard_bias = LLViewerTexture::sDesiredDiscardBias;
    F32 cache_usage = (F32)LLAppViewer::getTextureCache()->getUsage().valueInUnits<LLUnits::Megabytes>();
    F32 cache_max_usage = (F32)LLAppViewer::getTextureCache()->getMaxUsage().valueInUnits<LLUnits::Megabytes>();
    S32 line_height = LLFontGL::getFontMonospace()->getLineHeight();
    S32 v_offset = 0;
    F32Bytes total_texture_downloaded = gTotalTextureData;
    F32Bytes total_object_downloaded = gTotalObjectData;
    U32 total_http_requests = LLAppViewer::getTextureFetch()->getTotalNumHTTPRequests();
    U32 total_active_cached_objects = LLWorld::getInstance()->getNumOfActiveCachedObjects();
    U32 total_objects = gObjectList.getNumObjects();
    F32 x_right = 0.0;
    // Right-edge of the widest status line drawn this frame; published to the
    // file-scope max_status_w at the bottom so the view grows to fit.
    S32 widest = 0;
    const LLFontGL* font_mono = LLFontGL::getFontMonospace();

    U32 image_count = gTextureList.getNumImages();
    U32 raw_image_count = 0;
    U64 raw_image_bytes = 0;

    U32 saved_raw_image_count = 0;
    U64 saved_raw_image_bytes = 0;

    U32 aux_raw_image_count = 0;
    U64 aux_raw_image_bytes = 0;

    for (auto& image : gTextureList)
    {
        const LLImageRaw* raw_image = image->getRawImage();

        if (raw_image)
        {
            raw_image_count++;
            raw_image_bytes += raw_image->getDataSize();
        }

        raw_image = image->getSavedRawImage();
        if (raw_image)
        {
            saved_raw_image_count++;
            saved_raw_image_bytes += raw_image->getDataSize();
        }

        raw_image = image->getAuxRawImage();
        if (raw_image)
        {
            aux_raw_image_count++;
            aux_raw_image_bytes += raw_image->getDataSize();
        }
    }

   F64 raw_image_bytes_MB = raw_image_bytes / (1024.0 * 1024.0);
   F64 saved_raw_image_bytes_MB = saved_raw_image_bytes / (1024.0 * 1024.0);
   F64 aux_raw_image_bytes_MB = aux_raw_image_bytes / (1024.0 * 1024.0);
   F64 texture_bytes_alloc = LLImageGL::getTextureBytesAllocated() / 1024.0 / 1024.0;
   F64 vertex_bytes_alloc = LLVertexBuffer::getBytesAllocated() / 1024.0 / 512.0;
   F64 render_bytes_alloc = LLRenderTarget::sBytesAllocated / 1024.0 / 1024.0;

    //----------------------------------------------------------------------------
    LLGLSUIDefault gls_ui;
    LLColor4 text_color(1.f, 1.f, 1.f, 0.75f);
    LLColor4 color;

    std::string text = "";

    LLTrace::Recording& recording = LLViewerStats::instance().getRecording();

    F64 cacheHits     = recording.getSampleCount(LLTextureFetch::sCacheHit);
    F64 cacheAttempts = recording.getSampleCount(LLTextureFetch::sCacheAttempt);

    F32 cacheHitRate = (cacheAttempts > 0.0) ? F32((cacheHits / cacheAttempts) * 100.0f) : 0.0f;

    U32 cacheReadLatMin = U32(recording.getMin(LLTextureFetch::sCacheReadLatency).value() * 1000.0f);
    U32 cacheReadLatMed = U32(recording.getMean(LLTextureFetch::sCacheReadLatency).value() * 1000.0f);
    U32 cacheReadLatMax = U32(recording.getMax(LLTextureFetch::sCacheReadLatency).value() * 1000.0f);

    U32 texDecodeLatMin = U32(recording.getMin(LLTextureFetch::sTexDecodeLatency).value() * 1000.0f);
    U32 texDecodeLatMed = U32(recording.getMean(LLTextureFetch::sTexDecodeLatency).value() * 1000.0f);
    U32 texDecodeLatMax = U32(recording.getMax(LLTextureFetch::sTexDecodeLatency).value() * 1000.0f);

    U32 texFetchLatMin = U32(recording.getMin(LLTextureFetch::sTexFetchLatency).value() * 1000.0f);
    U32 texFetchLatMed = U32(recording.getMean(LLTextureFetch::sTexFetchLatency).value() * 1000.0f);
    U32 texFetchLatMax = U32(recording.getMax(LLTextureFetch::sTexFetchLatency).value() * 1000.0f);

    // Parent LLContainerView already paints the full-rect background; this bar's
    // required height (see getRequiredRect) covers all eight lines of text below.
    text = llformat("Est. Free: %d MB Sys Free: %d MB FBO: %d MB Probe#: %d Probe Mem: %d MB Bias: %.2f Cache: %.1f/%.1f MB",
                    (S32)LLViewerTexture::sFreeVRAMMegabytes,
                    LLMemory::getAvailableMemKB()/1024,
                    LLRenderTarget::sBytesAllocated/(1024*1024),
                    gPipeline.mReflectionMapManager.probeCount(),
                    gPipeline.mReflectionMapManager.probeMemory(),
                    discard_bias,
                    cache_usage,
                    cache_max_usage);
    widest = llmax(widest, font_mono->getWidth(text));
    font_mono->renderUTF8(text, 0, 0, v_offset + line_height*8,
                          text_color, LLFontGL::LEFT, LLFontGL::TOP);

    text = llformat("Images: %d   Raw: %d (%.2f MB)  Saved: %d (%.2f MB) Aux: %d (%.2f MB)", image_count, raw_image_count, raw_image_bytes_MB,
        saved_raw_image_count, saved_raw_image_bytes_MB,
        aux_raw_image_count, aux_raw_image_bytes_MB);
    widest = llmax(widest, font_mono->getWidth(text));
    font_mono->renderUTF8(text, 0, 0, v_offset + line_height * 7,
        text_color, LLFontGL::LEFT, LLFontGL::TOP);

    text = llformat("Textures: %.2f MB  Vertex: %.2f MB  Render: %.2f MB  Total: %.2f MB",
                    texture_bytes_alloc,
                    vertex_bytes_alloc,
                    render_bytes_alloc,
        texture_bytes_alloc+vertex_bytes_alloc);
    widest = llmax(widest, font_mono->getWidth(text));
    font_mono->renderUTF8(text, 0, 0, v_offset + line_height * 6,
        text_color, LLFontGL::LEFT, LLFontGL::TOP);

    U32 cache_read(0U), cache_write(0U), res_wait(0U);
    LLAppViewer::getTextureFetch()->getStateStats(&cache_read, &cache_write, &res_wait);

    text = llformat("Net Tot Tex: %.1f MB Tot Obj: %.1f MB #Objs/#Cached: %d/%d Tot Htp: %d Cread: %u Cwrite: %u Rwait: %u",
                    total_texture_downloaded.valueInUnits<LLUnits::Megabytes>(),
                    total_object_downloaded.valueInUnits<LLUnits::Megabytes>(),
                    total_objects,
                    total_active_cached_objects,
                    total_http_requests,
                    cache_read,
                    cache_write,
                    res_wait);
    widest = llmax(widest, font_mono->getWidth(text));
    font_mono->renderUTF8(text, 0, 0, v_offset + line_height*5,
                          text_color, LLFontGL::LEFT, LLFontGL::TOP);

    text = llformat("CacheHitRate: %3.2f Read: %d/%d/%d Decode: %d/%d/%d Fetch: %d/%d/%d",
                    cacheHitRate,
                    cacheReadLatMin,
                    cacheReadLatMed,
                    cacheReadLatMax,
                    texDecodeLatMin,
                    texDecodeLatMed,
                    texDecodeLatMax,
                    texFetchLatMin,
                    texFetchLatMed,
                    texFetchLatMax);

    widest = llmax(widest, font_mono->getWidth(text));
    font_mono->renderUTF8(text, 0, 0, v_offset + line_height*4,
                          text_color, LLFontGL::LEFT, LLFontGL::TOP);

    //----------------------------------------------------------------------------

    text = llformat("Textures: %d Fetch: %d(%d) Cache R/W: %d/%d LFS:%d RAW:%d HTP:%d DEC:%d CRE:%d ",
                    gTextureList.getNumImages(),
                    LLAppViewer::getTextureFetch()->getNumRequests(), LLAppViewer::getTextureFetch()->getNumDeletes(),
                    LLAppViewer::getTextureCache()->getNumReads(), LLAppViewer::getTextureCache()->getNumWrites(),
                    LLLFSThread::sLocal->getPending(),
                    LLImageRaw::sRawImageCount,
                    LLAppViewer::getTextureFetch()->getNumHTTPRequests(),
                    LLAppViewer::getImageDecodeThread()->getPending(),
                    gTextureList.mCreateTextureList.size());

    x_right = 550.0f;
    font_mono->renderUTF8(text, 0, 0.f, (F32)(v_offset + line_height*3),
                          text_color, LLFontGL::LEFT, LLFontGL::TOP,
                          LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, S32_MAX, &x_right);

    F32Kilobits bandwidth(LLAppViewer::getTextureFetch()->getTextureBandwidth());
    F32Kilobits max_bandwidth(LLViewerThrottle::getMaxBandwidthKbps());
    color = bandwidth > max_bandwidth ? LLColor4::red : bandwidth > max_bandwidth*.75f ? LLColor4::yellow : text_color;
    color[VALPHA] = text_color[VALPHA];
    text = llformat("BW:%.0f/%.0f",bandwidth.value(), max_bandwidth.value());
    // x_right is the right-edge of the previous main segment; BW is drawn at it.
    widest = llmax(widest, (S32)x_right + font_mono->getWidth(text));
    font_mono->renderUTF8(text, 0, (S32)x_right, v_offset + line_height*3,
                          color, LLFontGL::LEFT, LLFontGL::TOP);

    // Mesh status line
    text = llformat("Mesh: Reqs(Tot/Htp/Big): %u/%u/%u Rtr/Err: %u/%u Cread/Cwrite: %u/%u Low/At/High: %d/%d/%d",
                    LLMeshRepository::sMeshRequestCount.load(), LLMeshRepository::sHTTPRequestCount.load(),
                    LLMeshRepository::sHTTPLargeRequestCount.load(),
                    LLMeshRepository::sHTTPRetryCount.load(), LLMeshRepository::sHTTPErrorCount.load(),
                    LLMeshRepository::sCacheReads.load(), LLMeshRepository::sCacheWrites.load(),
                    LLMeshRepoThread::sRequestLowWater.load(), LLMeshRepoThread::sRequestWaterLevel.load(),
                    LLMeshRepoThread::sRequestHighWater.load());
    widest = llmax(widest, font_mono->getWidth(text));
    font_mono->renderUTF8(text, 0, 0, v_offset + line_height*2,
                          text_color, LLFontGL::LEFT, LLFontGL::TOP);

    // Header for texture table columns
    S32 dx1 = 0;
    if (LLAppViewer::getTextureFetch()->mDebugPause)
    {
        LLFontGL::getFontMonospace()->renderUTF8(std::string("!"), 0, title_x1, v_offset + line_height,
                                         text_color, LLFontGL::LEFT, LLFontGL::TOP);
        dx1 += char_w;
    }
    if (mTextureView->mFreezeView)
    {
        LLFontGL::getFontMonospace()->renderUTF8(std::string("*"), 0, title_x1 + dx1, v_offset + line_height,
                                         text_color, LLFontGL::LEFT, LLFontGL::TOP);
        dx1 += char_w;
    }
    LLFontGL::getFontMonospace()->renderUTF8(title_string1, 0, title_x1+dx1, v_offset + line_height,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);

    LLFontGL::getFontMonospace()->renderUTF8(title_string2, 0, title_x2, v_offset + line_height,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);

    LLFontGL::getFontMonospace()->renderUTF8(title_string3, 0, title_x3, v_offset + line_height,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);

    LLFontGL::getFontMonospace()->renderUTF8(title_string4, 0, title_x4, v_offset + line_height,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);

    // Publish for next frame's required_view_width(). Column-header widths are
    // captured separately via title_x4 in update_layout(), so we only track the
    // non-header status lines here.
    max_status_w = widest;
}

bool LLGLTexMemBar::handleMouseDown(S32 x, S32 y, MASK mask)
{
    return false;
}

LLRect LLGLTexMemBar::getRequiredRect()
{
    LLRect rect;
    // draw() renders eight lines of text at v_offset + line_height * (1..8).
    rect.mTop = LLFontGL::getFontMonospace()->getLineHeight() * 8;
    return rect;
}

////////////////////////////////////////////////////////////////////////////
class LLGLTexSizeBar
{
public:
    LLGLTexSizeBar(S32 index, S32 left, S32 bottom, S32 right, S32 line_height)
    {
        mIndex = index ;
        mLeft = left ;
        mBottom = bottom ;
        mRight = right ;
        mLineHeight = line_height ;
        mTopLoaded = 0 ;
        mTopBound = 0 ;
        mScale = 1.0f ;
    }

    void setTop(S32 loaded, S32 bound, F32 scale) {mTopLoaded = loaded ; mTopBound = bound; mScale = scale ;}

    void draw();
    bool handleHover(S32 x, S32 y, MASK mask, bool set_pick_size) ;

private:
    S32 mIndex ;
    S32 mLeft ;
    S32 mBottom ;
    S32 mRight ;
    S32 mTopLoaded ;
    S32 mTopBound ;
    S32 mLineHeight ;
    F32 mScale ;
};

bool LLGLTexSizeBar::handleHover(S32 x, S32 y, MASK mask, bool set_pick_size)
{
    if(y > mBottom && (y < mBottom + (S32)(mTopLoaded * mScale) || y < mBottom + (S32)(mTopBound * mScale)))
    {
        LLImageGL::setCurTexSizebar(mIndex, set_pick_size);
    }
    return true ;
}
void LLGLTexSizeBar::draw()
{
    LLGLSUIDefault gls_ui;

    if(LLImageGL::sCurTexSizeBar == mIndex)
    {
        LLColor4 text_color(1.f, 1.f, 1.f, 0.75f);
        std::string text;

        text = llformat("%d", mTopLoaded) ;
        LLFontGL::getFontMonospace()->renderUTF8(text, 0, mLeft, mBottom + (S32)(mTopLoaded * mScale) + mLineHeight,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);

        text = llformat("%d", mTopBound) ;
        LLFontGL::getFontMonospace()->renderUTF8(text, 0, (mLeft + mRight) / 2, mBottom + (S32)(mTopBound * mScale) + mLineHeight,
                                     text_color, LLFontGL::LEFT, LLFontGL::TOP);
    }

    LLColor4 loaded_color(1.0f, 0.0f, 0.0f, 0.75f);
    LLColor4 bound_color(1.0f, 1.0f, 0.0f, 0.75f);
    gl_rect_2d(mLeft, mBottom + (S32)(mTopLoaded * mScale), (mLeft + mRight) / 2, mBottom, loaded_color) ;
    gl_rect_2d((mLeft + mRight) / 2, mBottom + (S32)(mTopBound * mScale), mRight, mBottom, bound_color) ;
}
////////////////////////////////////////////////////////////////////////////

LLTextureView::LLTextureView(const LLTextureView::Params& p)
    :   LLContainerView(p),
        mFreezeView(false),
        mOrderFetch(false),
        mPrintList(false),
        mNumTextureBars(0)
{
    setVisible(false);

    setDisplayChildren(true);
    mGLTexMemBar = 0;
    mAvatarTexBar = 0;
}

LLTextureView::~LLTextureView()
{
    // Children all cleaned up by default view destructor.
    delete mGLTexMemBar;
    mGLTexMemBar = 0;

    delete mAvatarTexBar;
    mAvatarTexBar = 0;
}

typedef std::pair<F32,LLViewerFetchedTexture*> decode_pair_t;
struct compare_decode_pair
{
    bool operator()(const decode_pair_t& a, const decode_pair_t& b) const
    {
        return a.first > b.first;
    }
};

struct KillView
{
    void operator()(LLView* viewp)
    {
        viewp->getParent()->removeChild(viewp);
        viewp->die();
    }
};

void LLTextureView::draw()
{
    if (!mFreezeView)
    {
        // Recompute column positions for the current font, then make sure this
        // view is wide enough to hold all the content + LLContainerView padding.
        update_layout();
        const S32 req_w = required_view_width();
        if (getRect().getWidth() != req_w)
        {
            LLRect r = getRect();
            r.mRight = r.mLeft + req_w;
            setRect(r);
        }

        for_each(mTextureBars.begin(), mTextureBars.end(), KillView());
        mTextureBars.clear();

        if (mGLTexMemBar)
        {
            removeChild(mGLTexMemBar);
            mGLTexMemBar->die();
            mGLTexMemBar = 0;
        }

        if (mAvatarTexBar)
        {
            removeChild(mAvatarTexBar);
            mAvatarTexBar->die();
            mAvatarTexBar = 0;
        }

        typedef std::multiset<decode_pair_t, compare_decode_pair > display_list_t;
        display_list_t display_image_list;

        if (mPrintList)
        {
            LL_INFOS() << "ID\tMEM\tBOOST\tPRI\tWIDTH\tHEIGHT\tDISCARD" << LL_ENDL;
        }

        for (LLViewerTextureList::image_list_t::iterator iter = gTextureList.mImageList.begin();
             iter != gTextureList.mImageList.end(); )
        {
            LLViewerFetchedTexture* imagep = *iter++;
            if(!imagep->hasFetcher())
            {
                continue ;
            }

            S32 cur_discard = imagep->getDiscardLevel();
            S32 desired_discard = imagep->mDesiredDiscardLevel;

            if (mPrintList)
            {
                S32 tex_mem = imagep->hasGLTexture() ? imagep->getTextureMemory().value() : 0 ;
                LL_INFOS() << imagep->getID()
                        << "\t" << tex_mem
                        << "\t" << imagep->getBoostLevel()
                        << "\t" << imagep->getMaxVirtualSize()
                        << "\t" << imagep->getWidth()
                        << "\t" << imagep->getHeight()
                        << "\t" << cur_discard
                        << LL_ENDL;
            }

            if (imagep->getID() == LLAppViewer::getTextureFetch()->mDebugID)
            {
//              static S32 debug_count = 0;
//              ++debug_count; // for breakpoints
            }

            F32 pri;
            if (mOrderFetch)
            {
                pri = ((F32)imagep->mFetchPriority)/256.f;
            }
            else
            {
                pri = imagep->getMaxVirtualSize();
            }
            pri = llclamp(pri, 0.0f, HIGH_PRIORITY-1.f);

            if (sDebugImages.find(imagep) != sDebugImages.end())
            {
                pri += 4*HIGH_PRIORITY;
            }

            if (!mOrderFetch)
            {
                if (pri < HIGH_PRIORITY && LLSelectMgr::getInstance())
                {
                    struct f : public LLSelectedTEFunctor
                    {
                        LLViewerFetchedTexture* mImage;
                        f(LLViewerFetchedTexture* image) : mImage(image) {}
                        virtual bool apply(LLViewerObject* object, S32 te)
                        {
                            return (mImage == object->getTEImage(te));
                        }
                    } func(imagep);
                    const bool firstonly = true;
                    bool match = LLSelectMgr::getInstance()->getSelection()->applyToTEs(&func, firstonly);
                    if (match)
                    {
                        pri += 3*HIGH_PRIORITY;
                    }
                }

                if (pri < HIGH_PRIORITY && (cur_discard< 0 || desired_discard < cur_discard))
                {
                    LLSelectNode* hover_node = LLSelectMgr::instance().getHoverNode();
                    if (hover_node)
                    {
                        LLViewerObject *objectp = hover_node->getObject();
                        if (objectp)
                        {
                            S32 tex_count = objectp->getNumTEs();
                            for (S32 i = 0; i < tex_count; i++)
                            {
                                if (imagep == objectp->getTEImage(i))
                                {
                                    pri += 2*HIGH_PRIORITY;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (pri > 0.f && pri < HIGH_PRIORITY)
                {
                    if (imagep->mLastPacketTimer.getElapsedTimeF32() < 1.f ||
                        imagep->mFetchDeltaTime < 0.25f)
                    {
                        pri += 1*HIGH_PRIORITY;
                    }
                }
            }

            if (pri > 0.0f)
            {
                display_image_list.insert(std::make_pair(pri, imagep));
            }
        }

        if (mPrintList)
        {
            mPrintList = false;
        }

        static S32 max_count = 50;
        S32 count = 0;
        mNumTextureBars = 0 ;
        for (display_list_t::iterator iter = display_image_list.begin();
             iter != display_image_list.end(); iter++)
        {
            LLViewerFetchedTexture* imagep = iter->second;
            S32 hilite = 0;
            F32 pri = iter->first;
            if (pri >= 1 * HIGH_PRIORITY)
            {
                hilite = (S32)((pri+1) / HIGH_PRIORITY) - 1;
            }
            if ((hilite || count < max_count-10) && (count < max_count))
            {
                if (addBar(imagep, hilite))
                {
                    count++;
                }
            }
        }

        if (mOrderFetch)
            sortChildren(LLTextureBar::sort_fetch());
        else
            sortChildren(LLTextureBar::sort());

        LLGLTexMemBar::Params tmbp;
        LLRect tmbr;
        tmbp.name("gl texmem bar");
        tmbp.rect(tmbr);
        tmbp.follows.flags = FOLLOWS_LEFT|FOLLOWS_TOP;
        tmbp.texture_view(this);
        mGLTexMemBar = LLUICtrlFactory::create<LLGLTexMemBar>(tmbp);
        addChild(mGLTexMemBar);
        sendChildToFront(mGLTexMemBar);

        LLAvatarTexBar::Params atbp;
        LLRect atbr;
        atbp.name("gl avatartex bar");
        atbp.texture_view(this);
        atbp.rect(atbr);
        mAvatarTexBar = LLUICtrlFactory::create<LLAvatarTexBar>(atbp);
        addChild(mAvatarTexBar);
        sendChildToFront(mAvatarTexBar);

        reshape(getRect().getWidth(), getRect().getHeight(), true);

        LLUI::popMatrix();
        LLUI::pushMatrix();
        LLUI::translate((F32)getRect().mLeft, (F32)getRect().mBottom);

        for (child_list_const_iter_t child_iter = getChildList()->begin();
             child_iter != getChildList()->end(); ++child_iter)
        {
            LLView *viewp = *child_iter;
            if (viewp->getRect().mBottom < 0)
            {
                viewp->setVisible(false);
            }
        }
    }

    LLContainerView::draw();

}

bool LLTextureView::addBar(LLViewerFetchedTexture *imagep, S32 hilite)
{
    llassert(imagep);

    LLTextureBar *barp;
    LLRect r;

    mNumTextureBars++;

    LLTextureBar::Params tbp;
    tbp.name("texture bar");
    tbp.rect(r);
    tbp.texture_view(this);
    barp = LLUICtrlFactory::create<LLTextureBar>(tbp);
    barp->mImagep = imagep;
    barp->mHilite = hilite;

    addChild(barp);
    mTextureBars.push_back(barp);

    return true;
}

bool LLTextureView::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if ((mask & (MASK_CONTROL|MASK_SHIFT|MASK_ALT)) == (MASK_ALT|MASK_SHIFT))
    {
        mPrintList = true;
        return true;
    }
    if ((mask & (MASK_CONTROL|MASK_SHIFT|MASK_ALT)) == (MASK_CONTROL|MASK_SHIFT))
    {
        LLAppViewer::getTextureFetch()->mDebugPause = !LLAppViewer::getTextureFetch()->mDebugPause;
        return true;
    }
    if (mask & MASK_SHIFT)
    {
        mFreezeView = !mFreezeView;
        return true;
    }
    if (mask & MASK_CONTROL)
    {
        mOrderFetch = !mOrderFetch;
        return true;
    }
    return LLView::handleMouseDown(x,y,mask);
}

bool LLTextureView::handleMouseUp(S32 x, S32 y, MASK mask)
{
    return false;
}

bool LLTextureView::handleKey(KEY key, MASK mask, bool called_from_parent)
{
    return false;
}


