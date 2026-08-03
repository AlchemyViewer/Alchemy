/**
 * @file lltexunit.h
 * @brief LLTexUnit definition
 *
 * Split out of llrender.h so that headers describing texture *sampling* -- notably
 * alsamplerstate.h, which needs these enums -- can be included by llrender.h without a
 * cycle. Nothing about the class changed in the move.
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

#pragma once

#include "llglheaders.h"

class LLCubeMap;
class LLImageGL;
class LLRender;
class LLRenderTarget;
class LLTexture;

constexpr U32 LL_NUM_TEXTURE_LAYERS = 32;

class LLTexUnit
{
    friend class LLRender;
public:
    static U32 sWhiteTexture;

    typedef enum
    {
        TT_TEXTURE = 0,         // Standard 2D Texture
        TT_RECT_TEXTURE,        // Non power of 2 texture
        TT_CUBE_MAP,            // 6-sided cube map texture
        TT_CUBE_MAP_ARRAY,      // Array of cube maps
        TT_MULTISAMPLE_TEXTURE, // see GL_ARB_texture_multisample
        TT_TEXTURE_3D,          // standard 3D Texture
        TT_NONE,                // No texture type is currently enabled
    } eTextureType;

    typedef enum
    {
        TAM_WRAP = 0,           // Standard 2D Texture
        TAM_MIRROR,             // Non power of 2 texture
        TAM_CLAMP               // No texture type is currently enabled
    } eTextureAddressMode;

    typedef enum
    {   // Note: If mipmapping or anisotropic are not enabled or supported it should fall back gracefully
        TFO_POINT = 0,          // Equal to: min=point, mag=point, mip=none.
        TFO_BILINEAR,           // Equal to: min=linear, mag=linear, mip=point.
        TFO_TRILINEAR,          // Equal to: min=linear, mag=linear, mip=linear.
        TFO_ANISOTROPIC         // Equal to: min=anisotropic, max=anisotropic, mip=linear.
    } eTextureFilterOptions;

    typedef enum
    {
        TMG_NONE = 0,           // Mipmaps are not automatically generated for this texture.
        TMG_AUTO,               // Mipmaps are automatically generated for this texture.
        TMG_MANUAL              // Mipmaps are manually generated for this texture.
    } eTextureMipGeneration;

    typedef enum
    {
        TB_REPLACE = 0,
        TB_ADD,
        TB_MULT,
        TB_MULT_X2,
        TB_ALPHA_BLEND,
        TB_COMBINE          // Doesn't need to be set directly, setTexture___Blend() set TB_COMBINE automatically
    } eTextureBlendType;

    typedef enum
    {
        TBO_REPLACE = 0,            // Use Source 1
        TBO_MULT,                   // Multiply: ( Source1 * Source2 )
        TBO_MULT_X2,                // Multiply then scale by 2:  ( 2.0 * ( Source1 * Source2 ) )
        TBO_MULT_X4,                // Multiply then scale by 4:  ( 4.0 * ( Source1 * Source2 ) )
        TBO_ADD,                    // Add: ( Source1 + Source2 )
        TBO_ADD_SIGNED,             // Add then subtract 0.5: ( ( Source1 + Source2 ) - 0.5 )
        TBO_SUBTRACT,               // Subtract Source2 from Source1: ( Source1 - Source2 )
        TBO_LERP_VERT_ALPHA,        // Interpolate based on Vertex Alpha (VA): ( Source1 * VA + Source2 * (1-VA) )
        TBO_LERP_TEX_ALPHA,         // Interpolate based on Texture Alpha (TA): ( Source1 * TA + Source2 * (1-TA) )
        TBO_LERP_PREV_ALPHA,        // Interpolate based on Previous Alpha (PA): ( Source1 * PA + Source2 * (1-PA) )
        TBO_LERP_CONST_ALPHA        // Interpolate based on Const Alpha (CA): ( Source1 * CA + Source2 * (1-CA) )
    } eTextureBlendOp;

    typedef enum
    {
        TBS_PREV_COLOR = 0,         // Color from the previous texture stage
        TBS_PREV_ALPHA,
        TBS_ONE_MINUS_PREV_COLOR,
        TBS_ONE_MINUS_PREV_ALPHA,
        TBS_TEX_COLOR,              // Color from the texture bound to this stage
        TBS_TEX_ALPHA,
        TBS_ONE_MINUS_TEX_COLOR,
        TBS_ONE_MINUS_TEX_ALPHA,
        TBS_VERT_COLOR,             // The vertex color currently set
        TBS_VERT_ALPHA,
        TBS_ONE_MINUS_VERT_COLOR,
        TBS_ONE_MINUS_VERT_ALPHA,
        TBS_CONST_COLOR,            // The constant color value currently set
        TBS_CONST_ALPHA,
        TBS_ONE_MINUS_CONST_COLOR,
        TBS_ONE_MINUS_CONST_ALPHA
    } eTextureBlendSrc;

    typedef enum
    {
        TCS_LINEAR = 0,
        TCS_SRGB
    } eTextureColorSpace;

    LLTexUnit(S32 index = -1);

    // Refreshes renderer state of the texture unit to the cached values
    // Needed when the render context has changed and invalidated the current state
    void refreshState(void);

    // returns the index of this texture unit
    S32 getIndex(void) const { return mIndex; }

    // Sets this tex unit to be the currently active one
    void activate(void);

    // Enables this texture unit for the given texture type
    // (automatically disables any previously enabled texture type)
    void enable(eTextureType type);

    // Disables the current texture unit
    void disable(void);

    // Binds the LLImageGL to this texture unit
    // (automatically enables the unit for the LLImageGL's texture type)
    bool bind(LLImageGL* texture, bool for_rendering = false, bool forceBind = false, S32 usename = 0);
    bool bind(LLTexture* texture, bool for_rendering = false, bool forceBind = false);

    // Bind AND select the sampler in one step, overriding part of the texture's own
    // sampling mode for this binding only.
    //
    // Prefer these over bind() followed by setSamplerAddressMode/setSamplerFilteringOption:
    // that sequence resolves a sampler from the texture, binds it, then immediately binds a
    // second one -- two glBindSampler calls plus a gGL.flush() between them, which splits
    // the draw batch for a sampler that was about to be replaced.
    //
    // The one-argument form keeps the texture's own filter, which is what callers
    // overriding only the wrap mode (terrain, avatar bake masks) want.
    bool bindSampled(LLTexture* texture, eTextureAddressMode address, bool forceBind = false);
    bool bindSampled(LLTexture* texture, eTextureFilterOptions filter,
                     eTextureAddressMode address, bool forceBind = false);

    // bind implementation for inner loops
    // makes the following assumptions:
    //  - No need for gGL.flush()
    //  - texture is not null
    //  - gl_tex->getTexName() is not zero
    //  - This texture is not being bound redundantly
    //  - USE_SRGB_DECODE is disabled
    //  - mTexOptionsDirty is false
    //  -
    void bindFast(LLTexture* texture);

    // Binds a cubemap to this texture unit
    // (automatically enables the texture unit for cubemaps)
    bool bind(LLCubeMap* cubeMap);

    // Binds a render target to this texture unit
    // (automatically enables the texture unit for the RT's texture type)
    // sampler is an ALSamplerCache name, or 0 for the texture object's own state.
    bool bind(LLRenderTarget * renderTarget, bool bindDepth = false, U32 sampler = 0);

    // Manually binds a texture to the texture unit
    // (automatically enables the tex unit for the given texture type)
    // sampler is an ALSamplerCache name, or 0 to sample through the texture object's own
    // state. Applied even when the texture binding is unchanged -- see bindSampler.
    bool bindManual(eTextureType type, U32 texture, bool hasMips = false, U32 sampler = 0);

    // Unbinds the currently bound texture of the given type
    // (only if there's a texture of the given type currently bound)
    void unbind(eTextureType type);

    // Fast but unsafe version of unbind
    void unbindFast(eTextureType type);

    // Writes the addressing mode onto the TEXTURE OBJECT bound to this unit, which only
    // takes effect while the unit samples through sampler 0 -- a bound sampler object
    // overrides wrap state wholesale. For anything LLImageGL-backed use
    // LLImageGL::setAddressMode, which feeds sampler selection; this is for render targets
    // and raw GL names. Warns once if a sampler is bound.
    //
    // Warning: this stays set for the bound texture forever,
    // make sure you want to permanently change the address mode  for the bound texture.
    void setTextureAddressMode(eTextureAddressMode mode);
    // MUST already be active and bound
    void setTextureAddressModeFast(eTextureAddressMode mode, eTextureType tex_type);

    // See setTextureAddressMode: texture-object state, meaningful only under sampler 0.
    //
    // Warning: this stays set for the bound texture forever,
    // make sure you want to permanently change the filtering for the bound texture.
    void setTextureFilteringOption(LLTexUnit::eTextureFilterOptions option);
    // MUST already be active and bound
    void setTextureFilteringOptionFast(LLTexUnit::eTextureFilterOptions option, eTextureType tex_type);

    // Select the sampler object this unit samples through, from ALSamplerCache.
    //
    // Pass 0 to fall back to the bound texture object's own filter/wrap/compare state.
    // A sampler object OVERRIDES that state wholesale while bound, so a leftover
    // glTexParameteri on a unit with a non-zero sampler is a silent no-op -- when
    // converting a bind path, convert every filtering call that follows it.
    //
    // Never selects a sampler for TT_MULTISAMPLE_TEXTURE: sampling a multisample
    // texture through a sampler object is an error at draw time.
    void bindSampler(U32 sampler);

    // Change how THIS BINDING samples, without touching the texture object.
    //
    // The bind recorded the texture's own filter/address; these re-select a sampler with
    // one half swapped, so the change lasts until the next bind and affects nobody else.
    // That is the point: terrain wants its detail textures wrapped, but those are shared
    // LLViewerTextures, and writing TAM_WRAP onto the object leaked the choice into every
    // other user of the same texture.
    void setSamplerAddressMode(eTextureAddressMode mode);
    void setSamplerFilteringOption(eTextureFilterOptions option);

    static U32 getInternalType(eTextureType type);

    U32 getCurrTexture(void) { return mCurrTexture; }

    eTextureType getCurrType(void) { return mCurrTexType; }

    void setHasMipMaps(bool hasMips) { mHasMipMaps = hasMips; }

protected:
    friend class LLRender;

    S32                 mIndex;
    U32                 mCurrTexture;
    eTextureType        mCurrTexType;
    bool                mHasMipMaps;
    U32                 mCurrSampler = 0;
    // The sampling mode the current binding was selected with, so setSampler*() can swap
    // one half and keep the other. Tracked on LLImageGL binds, where a texture carries its
    // own mode; render-target binds pass a resolved sampler and leave these at defaults.
    eTextureFilterOptions  mCurrFilter  = TFO_ANISOTROPIC;
    eTextureAddressMode    mCurrAddress = TAM_WRAP;

    void debugTextureUnit(void);

    // LL_WARNS_ONCE if a sampler object is bound here, where texture-object sampling state
    // is silently ignored. See the note on setTextureAddressMode.
    void warnIfSamplerBound(const char* who) const;

    // Sentinel for bindImpl's overrides: take that half of the sampling mode from the
    // texture. Not a valid eTextureFilterOptions / eTextureAddressMode value.
    static constexpr S32 KEEP_TEXTURE_SAMPLING = -1;

    bool bindImpl(LLTexture* texture, bool forceBind, S32 filter_override, S32 address_override);
};
