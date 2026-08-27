/**
 * @file llfontgl.h
 * @author Doug Soo
 * @brief Wrapper around FreeType
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLFONTGL_H
#define LL_LLFONTGL_H

#include "llcoord.h"
#include "llfontregistry.h"
#include "llimagegl.h"
#include "llpointer.h"
#include "llrect.h"
#include "v2math.h"

class LLColor4;
// Key used to request a font.
class LLFontDescriptor;
class LLFontFreetype;

// Structure used to store previously requested fonts.
class LLFontRegistry;

class LLFontGL
{
public:
    enum HAlign
    {
        // Horizontal location of x,y coord to render.
        LEFT = 0,       // Left align
        RIGHT = 1,      // Right align
        HCENTER = 2,    // Center
    };

    enum VAlign
    {
        // Vertical location of x,y coord to render.
        TOP = 3,        // Top align
        VCENTER = 4,    // Center
        BASELINE = 5,   // Baseline
        BOTTOM = 6      // Bottom
    };

    enum StyleFlags
    {
        // text style to render.  May be combined (these are bit flags)
        NORMAL    = 0x00,
        BOLD      = 0x01,
        ITALIC    = 0x02,
        UNDERLINE = 0x04
    };

    enum ShadowType
    {
        NO_SHADOW,
        DROP_SHADOW,
        DROP_SHADOW_SOFT
    };

    LLFontGL();
    ~LLFontGL();


    void reset(); // Reset a font after GL cleanup.  ONLY works on an already loaded font.

    void destroyGL();

    bool loadFace(const std::string& filename, F32 point_size, const F32 vert_dpi, const F32 horz_dpi, bool is_fallback, S32 face_n, EFontHinting hinting, S32 flags, const ALFontVarAxes& var_axes = {});

    S32 getNumFaces(const std::string& filename);
    // U64: the stamp is a SUM of per-face S32 generations; a 64-bit unsigned
    // accumulator keeps the strictly-increases contract without any overflow
    // horizon a real session could reach (an S32 sum could wrap UB-style).
    U64 getCacheGeneration() const;
    const LLFontFreetype* getFontFreetype() const { return mFontFreetype.get(); }

    S32 render(const LLWString &text, S32 begin_offset,
                const LLRect& rect,
                const LLColor4 &color,
                HAlign halign = LEFT,  VAlign valign = BASELINE,
                U8 style = NORMAL, ShadowType shadow = NO_SHADOW,
                S32 max_chars = S32_MAX,
                F32* right_x=NULL,
                bool use_ellipses = false,
                bool use_color = true) const;

    S32 render(const LLWString &text, S32 begin_offset,
                const LLRectf& rect,
                const LLColor4 &color,
                HAlign halign = LEFT,  VAlign valign = BASELINE,
                U8 style = NORMAL, ShadowType shadow = NO_SHADOW,
                S32 max_chars = S32_MAX,
                F32* right_x=NULL,
                bool use_ellipses = false,
                bool use_color = true) const;

    // on_pass_boundary, if non-null, is invoked once between the shadow pass and
    // the foreground pass when shadow != NO_SHADOW. LLFontVertexBuffer uses it to
    // close one captured display list and open another so each pass lands in its
    // own list with a uniform color across all vertices — a prerequisite for
    // color-only cache regeneration. For NO_SHADOW renders the callback is not
    // invoked (single-pass).
    typedef std::function<void()> pass_boundary_cb_t;
    S32 render(const LLWString &text, S32 begin_offset,
                F32 x, F32 y,
                const LLColor4 &color,
                HAlign halign = LEFT,  VAlign valign = BASELINE,
                U8 style = NORMAL, ShadowType shadow = NO_SHADOW,
                S32 max_chars = S32_MAX, S32 max_pixels = S32_MAX,
                F32* right_x=NULL,
                bool use_ellipses = false,
                bool use_color = true,
                pass_boundary_cb_t on_pass_boundary = nullptr) const;

    S32 render(const LLWString &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color) const;

    // renderUTF8 does a conversion, so is slower!
    S32 renderUTF8(const std::string &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color, HAlign halign,  VAlign valign, U8 style, ShadowType shadow, S32 max_chars = S32_MAX, S32 max_pixels = S32_MAX,  F32* right_x = NULL, bool use_ellipses = false, bool use_color = true) const;
    S32 renderUTF8(const std::string &text, S32 begin_offset, S32 x, S32 y, const LLColor4 &color) const;
    S32 renderUTF8(const std::string &text, S32 begin_offset, S32 x, S32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style = NORMAL, ShadowType shadow = NO_SHADOW) const;

    // font metrics - override for LLFontFreetype that returns units of virtual pixels
    F32 getAscenderHeight() const;
    F32 getDescenderHeight() const;
    S32 getLineHeight() const;     // ascender + descender (no line gap)
    S32 getLineSpacing() const;    // face->height — full baseline-to-baseline distance (includes line gap)

    // The wide forms carry their own length, so a caller measuring part of a
    // string passes a subview rather than a pointer plus a character budget the
    // callee cannot check. A bare llwchar* still converts (it is what U"..."
    // literals are), scanning to its terminator for the bound.
    S32 getWidth(const std::string& utf8text) const;
    S32 getWidth(LLWStringView wchars) const;
    S32 getWidth(const std::string& utf8text, S32 offset, S32 max_chars) const;
    S32 getWidth(LLWStringView wchars, S32 offset, S32 max_chars) const;

    F32 getWidthF32(const std::string& utf8text) const;
    F32 getWidthF32(LLWStringView wchars) const;
    F32 getWidthF32(const std::string& text, S32 offset, S32 max_chars) const;
    F32 getWidthF32(LLWStringView wchars, S32 offset, S32 max_chars, bool no_padding = false) const;

    // The following are called often, frequently with large buffers, so do not take
    // an owning string

    // Returns the max number of complete characters from text (up to max_chars) that can be drawn in max_pixels
    typedef enum e_word_wrap_style
    {
        ONLY_WORD_BOUNDARIES,
        WORD_BOUNDARY_IF_POSSIBLE,
        ANYWHERE
    } EWordWrapStyle ;
    S32 maxDrawableChars(LLWStringView wchars, F32 max_pixels, S32 max_chars = S32_MAX, EWordWrapStyle end_on_word_boundary = ANYWHERE) const;

    // Returns the index of the first complete characters from text that can be drawn in max_pixels
    // given that the character at start_pos should be the last character (or as close to last as possible).
    S32 firstDrawableChar(LLWStringView wchars, F32 max_pixels, S32 start_pos=S32_MAX, S32 max_chars = S32_MAX) const;

    // Returns the index of the character closest to pixel position x (ignoring text to the right of max_pixels and max_chars)
    S32 charFromPixelOffset(LLWStringView wchars, S32 char_offset, F32 x, F32 max_pixels=F32_MAX, S32 max_chars = S32_MAX, bool round = true) const;

    const LLFontDescriptor& getFontDesc() const;

    void generateASCIIglyphs();


    // Single entry point for both initial font system bring-up AND
    // subsequent reloads (UI scale change, AlchemyUIFontOverrides change,
    // skinned fonts.xml file change). On the first call, parses fonts.xml
    // and seeds default fonts. On every subsequent call, re-parses
    // fonts.xml + applies the current `font_overrides` and pointer-stable-
    // swaps each head's mFontFreetype to the freshly-loaded one. The
    // (screen_dpi, x_scale, y_scale) trio are written into the shared
    // sVertDPI / sHorizDPI / sScaleX / sScaleY statics that downstream
    // loadFace calls inside the rebuild read for new face keys.
    //
    // `font_overrides` is plumbed in from the caller (newview's
    // AlchemyUIFontOverrides setting) — llrender no longer reads
    // gSavedSettings on its own.
    static void initClass(F32 screen_dpi, F32 x_scale, F32 y_scale, const std::string& app_dir, const LLSD& font_overrides, bool create_gl_textures = true);

    // Variant of initClass that loads fonts.xml from `fonts_xml_path`
    // directly, bypassing gDirUtilp->findSkinnedFilenames. Single-shot:
    // only seeds a fresh registry — does not support reloads. Intended
    // for headless test bring-up where the skin tree isn't staged.
    static void initClass(F32 screen_dpi, F32 x_scale, F32 y_scale,
                          const std::string& app_dir,
                          const std::string& fonts_xml_path,
                          const LLSD& font_overrides,
                          bool create_gl_textures = true);

           void dumpTextures();
    static void dumpFonts();
    static void dumpFontTextures();

    // Load sans-serif, sans-serif-small, etc.
    // Slow, requires multiple seconds to load fonts.
    static bool loadDefaultFonts();
    static void loadCommonFonts();
    static void destroyDefaultFonts();
    static void destroyAllGL();

    // Mark a font reload as pending. Called from the AlchemyUIFontOverrides
    // setting listener. Coalesces multiple changes inside a single frame
    // into one reload. The caller (newview) drains the flag via
    // consumePendingReload() in checkSettings and re-invokes initClass with
    // the up-to-date overrides.
    static void schedulePendingReload();

    // Returns true exactly once if a reload was scheduled since the last
    // call (clearing the flag). Called from LLAppViewer::idle().
    static bool consumePendingReload();

    // Once-per-frame glyph cache sweep. Iterates all live LLFontFreetype
    // instances (heads + shared fallbacks) and runs each one's atlas-
    // sheet eviction sweep. Each freetype self-throttles to GC_INTERVAL_SEC,
    // so most calls are cheap no-ops. The viewer's frame loop drives this
    // via LLViewerWindow::checkSettings — DO NOT call from the render
    // hot path; the previous render-time call site paid the throttle-check
    // cost on every glyph render. Calling between frames also avoids
    // racing eviction with active glyph pointers.
    static void sweepGlyphCaches();

    // Mark fonts.xml content as having changed since the last parse.
    // Called by the live-file listener in newview when any skinned
    // fonts.xml mtime advances. The next initClass on an existing
    // registry will see this flag, take the full-reload (re-parse) path,
    // and clear the flag. UI scale changes that come in while this flag
    // is set still get the full reload — correctness over speed.
    static void markFontsXmlDirty();

    // User-selectable families declared in fonts.xml — see
    // LLFontRegistry::getAvailableFamilies for the filtering and ordering
    // rules. `filter` constrains the result by the per-family monospace
    // attribute. Returns an empty vector if the registry isn't ready.
    static std::vector<LLFontRegistry::FamilyInfo> getAvailableFamilies(
        LLFontRegistry::FamilyFilter filter = LLFontRegistry::FamilyFilter::ANY);

    // Takes a string with potentially several flags, i.e. "NORMAL|BOLD|ITALIC"
    static U8 getStyleFromString(const std::string &style);
    static const std::string& getStringFromStyle(U8 style);

    static std::string nameFromFont(const LLFontGL* fontp);
    static std::string sizeFromFont(const LLFontGL* fontp);

    static std::string nameFromHAlign(LLFontGL::HAlign align);
    static LLFontGL::HAlign hAlignFromName(const std::string& name);

    static std::string nameFromVAlign(LLFontGL::VAlign align);
    static LLFontGL::VAlign vAlignFromName(const std::string& name);

    static void setFontDisplay(bool flag) { sDisplayFont = flag; }

    static LLFontGL* getFontEmojiSmall();
    static LLFontGL* getFontEmojiMedium();
    static LLFontGL* getFontEmojiLarge();
    static LLFontGL* getFontEmojiHuge();
    static LLFontGL* getFontMonospace();
    static LLFontGL* getFontSansSerifSmall();
    static LLFontGL* getFontSansSerifSmallBold();
    static LLFontGL* getFontSansSerifSmallItalic();
    static LLFontGL* getFontSansSerif();
    static LLFontGL* getFontSansSerifMedium();
    static LLFontGL* getFontSansSerifBig();
    static LLFontGL* getFontSansSerifHuge();
    static LLFontGL* getFontSansSerifBold();
    static LLFontGL* getFont(const LLFontDescriptor& desc);
    // Use with legacy names like "SANSSERIF_SMALL" or "OCRA"
    static LLFontGL* getFontByName(const std::string& name);
    static LLFontGL* getFontDefault(); // default fallback font

    static std::string getFontPathLocal();
    static std::string getFontPathSystem();

    static LLCoordGL sCurOrigin;
    static F32          sCurDepth;
    static std::vector<std::pair<LLCoordGL, F32> > sOriginStack;

    static LLColor4 sShadowColor;

    // Shader-based shadow expansion: emit 1 dilated shadow quad per glyph and
    // let the fragment shader take 5 atlas taps instead of emitting 5 quads
    // per glyph from the CPU. Reduces DROP_SHADOW_SOFT geometry from 6 quads
    // per glyph to 2, cutting batch flush amplification by ~3x. Off by default
    // until per-atlas-type shader uniform plumbing is stable on mixed
    // text+emoji strings; flip via LLFontGL::enableShaderShadow().
    static bool sEnableShaderShadow;
    static void enableShaderShadow(bool enable) { sEnableShaderShadow = enable; }

    static F32 sVertDPI;
    static F32 sHorizDPI;
    static F32 sScaleX;
    static F32 sScaleY;
    static S32 sResolutionGeneration;
    // Set by markFontsXmlDirty(); consumed by initClass on the next call
    // to force the full-reload (re-parse) path even when overrides match.
    static bool sFontsXmlDirty;
    static bool sDisplayFont ;
    // Threaded in from newview's EmojiUseDarkPalette setting. Read by
    // ALFontFace::load when computing the CPAL palette index for COLRv1
    // faces; non-COLRv1 faces ignore it. Toggling the setting triggers a
    // font reload via the standard schedulePendingReload mechanism, so
    // every face re-runs ALFontFace::load and picks up the new value.
    static bool sUseDarkEmojiPalette;
    // Threaded in from newview's AlchemyForceMonochromeEmoji setting. When
    // true, LLFontGL::render downgrades Color → Grayscale lookups so COLRv1
    // emoji rasterize via the painter's luminance-shaded path and tint with
    // text_color via the renderer's existing Grayscale-tints-with-text rule.
    // Has no effect on non-COLRv1 color fonts (CBDT/sbix/SVG) — those keep
    // their native color rendering. Toggling triggers a font reload to
    // discard stale color-atlas glyphs.
    static bool sForceMonochromeEmoji;
    static std::string sAppDir;         // For loading fonts

private:
    friend class LLFontRegistry;
    friend class LLTextBillboard;
    friend class LLHUDText;

    LLFontGL(const LLFontGL &source) = delete;
    LLFontGL &operator=(const LLFontGL &source) = delete;

    LLFontDescriptor mFontDescriptor;
    LLPointer<LLFontFreetype> mFontFreetype;

    void renderTriangle(LLVector4a* vertex_out, LLVector2* uv_out, LLColor4U* colors_out, const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4U& color, F32 slant_amt) const;
    // Caller hoists shadow_color and italic slant_offset out of the glyph loop and
    // selects which half to emit. drawGlyphShadow is a no-op for NO_SHADOW or BOLD
    // (bold and shadow are mutually exclusive). drawGlyphForeground emits the BOLD
    // double-quad when style has BOLD set, otherwise a single quad.
    void drawGlyphShadow(S32& glyph_count, LLVector4a* vertex_out, LLVector2* uv_out, LLColor4U* colors_out, const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4U& shadow_color, ShadowType shadow, F32 slant_offset) const;
    void drawGlyphForeground(S32& glyph_count, LLVector4a* vertex_out, LLVector2* uv_out, LLColor4U* colors_out, const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4U& color, U8 style, F32 slant_offset) const;

    // Registry holds all instantiated fonts.
    static LLFontRegistry* sFontRegistry;
};

#endif
