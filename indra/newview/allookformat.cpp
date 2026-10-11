/**
 * @file allookformat.cpp
 * @brief A Lightbox Look as the text of a notecard: written, read, and checked value by value.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "linden_common.h"

#include "allookformat.h"

#include "alcurvemodel.h"
#include "llsdserialize.h"
#include "llstring.h"
#include "lluri.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <locale>
#include <map>
#include <sstream>

namespace
{
    const std::string METADATA_KEY_STRING(ALLookFormat::METADATA_KEY);
    const std::string NOTECARD_NAME_PREFIX_STRING(ALLookFormat::NOTECARD_NAME_PREFIX);

    // What the notecard says about itself, for whoever opens it: first in
    // the text, where a parser skips a comment (LLSDXMLFormatter writes no
    // XML declaration to put it under). Static text on purpose: a Look's
    // name or author written into it could carry "--", which ends an XML
    // comment early and leaves the rest of the notecard unreadable.
    const char* const NOTE = "<!-- Alchemy Lightbox Look. Drop this notecard on the Lightbox window, or open\n"
                             "     it and press Apply Look, to add it to your Looks. Only Lightbox settings\n"
                             "     are read from it, and anything malformed is ignored. -->\n";

    // The longest a Look's author or viewer is let be said to be.
    constexpr size_t PROVENANCE_BYTE_LIMIT = 128;

    // How far into a text the reserved key has to stand to count. A Look
    // notecard opens with the note and that key, well inside this; prose
    // that mentions the key far down is not one.
    constexpr size_t DETECT_WINDOW = 4096;

    // A text that is a number and nothing else. A dropdown bound to a numeric
    // setting writes its item's value as a string ("5"), and the settings
    // system reads it back as a number (LLSD's string_to_real), so a Look
    // carries such strings as a matter of course and they must be taken.
    // Taken strictly, though: the whole text has to be the number, so "5x"
    // and "half" are refused where LLSD would have read 5 and 0; and only
    // the spelling of a decimal number is read, so "nan", "inf" and a hex
    // float are refused on every platform alike -- MSVC's stream would
    // read "0x10" as sixteen where another's stops at the x. The classic
    // locale, so a comma decimal never passes or fails by the machine it
    // is read on.
    bool numberFromText(const std::string& text, F64& out)
    {
        std::string trimmed = text;
        LLStringUtil::trim(trimmed);
        if (trimmed.empty() || trimmed.find_first_not_of("0123456789+-.eE") != std::string::npos)
        {
            return false;
        }
        std::istringstream in(trimmed);
        in.imbue(std::locale::classic());
        F64 parsed = 0.0;
        in >> parsed;
        if (in.fail() || !in.eof() || !std::isfinite(parsed))
        {
            return false;
        }
        out = parsed;
        return true;
    }

    // A finite number from a Real, an Integer or a numeric text; false for
    // anything else, a NaN and an infinity among them.
    bool finiteNumber(const LLSD& value, F64& out)
    {
        if (value.isReal() || value.isInteger())
        {
            out = value.asReal();
            return std::isfinite(out);
        }
        if (value.isString())
        {
            return numberFromText(value.asStringRef(), out);
        }
        return false;
    }

    // A finite number that is still finite as the single-precision float an
    // F32, Vector3 or Color3 setting reads it as (convert_from_llsd in
    // llcontrol.cpp casts). LLSD holds doubles, so 1e39 is finite here and
    // infinity by the time the renderer sees it.
    bool finiteSingle(const LLSD& value, F64& out)
    {
        // Compared, not cast: narrowing a double past float range is undefined.
        return finiteNumber(value, out) && std::fabs(out) <= static_cast<F64>(std::numeric_limits<F32>::max());
    }

    // A number that is whole and within an S32, as an Integer.
    bool wholeNumber(const LLSD& value, LLSD& out)
    {
        if (value.isInteger())
        {
            out = LLSD::Integer(value.asInteger());
            return true;
        }
        F64 real = 0.0;
        if (!finiteNumber(value, real))
        {
            return false;
        }
        if (real != std::floor(real) || real < static_cast<F64>(std::numeric_limits<S32>::min()) ||
            real > static_cast<F64>(std::numeric_limits<S32>::max()))
        {
            return false;
        }
        out = LLSD::Integer(static_cast<S32>(real));
        return true;
    }

    // The Look as XML under the note. False where the formatter gave up on
    // it (LLSDXMLFormatter::format returns -1 out of memory), so that a
    // partial Look is never passed off as one; `text` is then empty.
    bool renderLook(const LLSD& body, bool pretty, std::string& text)
    {
        std::ostringstream xml;
        const S32          written = pretty ? LLSDSerialize::toPrettyXML(body, xml) : LLSDSerialize::toXML(body, xml);
        if (written < 0)
        {
            text.clear();
            return false;
        }
        text = NOTE + xml.str();
        return true;
    }

    // Each setting's row in the Lightbox (panel_lightbox_look.xml,
    // panel_lightbox_lens.xml): a slider's min_val and max_val, a colour
    // wheel's min_value and max_value, a swatch's 0 to 1 per channel, a
    // dropdown's item values. check_look_ranges.py reads this table and the
    // rows, and fails where they part.
    const std::map<std::string, ALLookFormat::SettingRange>& settingRanges()
    {
        using R = ALLookFormat::SettingRange;
        static const std::map<std::string, R> ranges = {
            // Look: tone mapping and exposure
            { "RenderExposure", R{ 0.5, 4.0, {} } },
            { "AlchemyRenderTonemapType", R{ 0.0, 6.0, { 0, 1, 2, 3, 4, 5, 6 } } },
            { "RenderTonemapMix", R{ 0.0, 1.0, {} } },
            { "RenderTonemapACESWhite", R{ 1.0, 16.0, {} } },
            { "RenderTonemapReinhardWhite", R{ 1.0, 16.0, {} } },
            { "RenderTonemapFilmicWhite", R{ 1.0, 16.0, {} } },
            { "RenderTonemapAgxContrast", R{ 0.5, 2.5, {} } },
            { "RenderTonemapAgxWhite", R{ 1.0, 32.0, {} } },
            { "AlchemyExposureCompensation", R{ -4.0, 4.0, {} } },
            { "AlchemyExposureMinEV", R{ -8.0, 0.0, {} } },
            { "AlchemyExposureMaxEV", R{ 0.0, 8.0, {} } },
            { "AlchemyExposureLowPercent", R{ 0.0, 50.0, {} } },
            { "AlchemyExposureHighPercent", R{ 50.0, 100.0, {} } },
            { "AlchemyExposureSpeedUp", R{ 0.1, 10.0, {} } },
            { "AlchemyExposureSpeedDown", R{ 0.1, 10.0, {} } },
            { "AlchemyExposureMeteringMode", R{ 0.0, 2.0, { 0, 1, 2 } } },
            { "AlchemyExposureSkyWeight", R{ 0.0, 1.0, {} } },
            // Look: colour grade
            { "RenderColorGradeLUTStrength", R{ 0.0, 1.0, {} } },
            { "RenderColorGradeBrightness", R{ -0.5, 0.5, {} } },
            { "RenderColorGradeContrast", R{ 0.0, 2.0, {} } },
            { "RenderColorGradeSaturation", R{ 0.0, 2.0, {} } },
            { "RenderColorGradeVibrance", R{ -1.0, 1.0, {} } },
            { "RenderColorGradeHighlights", R{ -1.0, 1.0, {} } },
            { "RenderColorGradeShadows", R{ -1.0, 1.0, {} } },
            { "RenderColorGradeHueShift", R{ -180.0, 180.0, {} } },
            { "RenderColorGradeBlackPoint", R{ 0.0, 0.5, {} } },
            { "RenderColorGradeWhitePoint", R{ 0.5, 1.0, {} } },
            { "RenderColorGradeWhiteBalanceCCT", R{ -5000.0, 5000.0, {} } },
            { "RenderColorGradeWhiteBalanceDuv", R{ -1.0, 1.0, {} } },
            { "RenderColorGradeLift", R{ -0.5, 0.5, {} } },
            { "RenderColorGradeGamma", R{ 0.5, 1.5, {} } },
            { "RenderColorGradeGain", R{ 0.5, 1.5, {} } },
            { "RenderColorGradeCurveAmount", R{ 0.0, 1.0, {} } },
            { "RenderSplitToneAmount", R{ 0.0, 1.0, {} } },
            { "RenderSplitToneBalance", R{ -1.0, 1.0, {} } },
            { "RenderSplitToneShadowTint", R{ 0.0, 1.0, {} } },
            { "RenderSplitToneHighlightTint", R{ 0.0, 1.0, {} } },
            { "RenderSplitToneMidtoneTint", R{ 0.0, 1.0, {} } },
            { "RenderSplitToneMidtoneAmount", R{ 0.0, 1.0, {} } },
            { "RenderSplitToneShadowWidth", R{ 0.02, 0.5, {} } },
            { "RenderSplitToneHighlightWidth", R{ 0.02, 0.5, {} } },
            // Lens: bloom and glow
            { "RenderBloomStrength", R{ 0.0, 2.0, {} } },
            { "RenderBloomThreshold", R{ 0.0, 8.0, {} } },
            { "RenderBloomKnee", R{ 0.0, 1.0, {} } },
            { "RenderBloomScatter", R{ 0.5, 2.5, {} } },
            { "RenderBloomAlphaGlowBoost", R{ 0.0, 8.0, {} } },
            { "RenderBloomHalationStrength", R{ 0.0, 1.0, {} } },
            { "RenderBloomHalationTint", R{ 0.0, 1.0, {} } },
            { "RenderGlowStrength", R{ 0.0, 1.0, {} } },
            { "RenderGlowWidth", R{ 0.0, 4.0, {} } },
            { "RenderGlowIterations", R{ 1.0, 8.0, {} } },
            { "RenderGlowLumWeights", R{ 0.0, 1.0, {} } },
            { "RenderGlowMaxExtractAlpha", R{ 0.0, 1.0, {} } },
            { "RenderGlowMinLuminance", R{ 0.0, 10000.0, {} } },
            { "RenderGlowWarmthAmount", R{ 0.0, 1.0, {} } },
            { "RenderGlowWarmthWeights", R{ 0.0, 2.0, {} } },
            // Lens: flare
            { "RenderLensFlareStrength", R{ 0.0, 1.0, {} } },
            { "RenderLensFlareStreakLength", R{ 0.01, 2.0, {} } },
            { "RenderLensFlareStreakFalloff", R{ 0.1, 10.0, {} } },
            { "RenderLensFlareStreakThickness", R{ 0.0, 1.0, {} } },
            { "RenderLensFlareStreakIntensity", R{ 0.0, 5.0, {} } },
            { "RenderLensFlareStreakTint", R{ 0.0, 1.0, {} } },
            { "RenderLensFlareChromaticSpread", R{ 0.0, 1.0, {} } },
            { "RenderLensFlareGlow", R{ 0.0, 5.0, {} } },
            { "RenderLensFlareGlowRadius", R{ 0.01, 0.5, {} } },
            { "RenderLensFlareGlowFalloff", R{ 1.0, 30.0, {} } },
            { "RenderLensFlareGhost", R{ 0.0, 5.0, {} } },
            { "RenderLensFlareGhostCount", R{ 0.0, 8.0, {} } },
            { "RenderLensFlareGhostSpacing", R{ 0.1, 1.0, {} } },
            { "RenderLensFlareHalo", R{ 0.0, 5.0, {} } },
            { "RenderLensFlareHaloRadius", R{ 0.01, 1.0, {} } },
            { "RenderLensFlareHaloWidth", R{ 0.01, 0.5, {} } },
            { "RenderLensFlareStarburst", R{ 0.0, 5.0, {} } },
            { "RenderLensFlareStarburstSpikes", R{ 1.0, 32.0, {} } },
            { "RenderLensFlareStarburstSharpness", R{ 1.0, 256.0, {} } },
            { "RenderLensFlareStarburstLength", R{ 0.0, 1.0, {} } },
            { "RenderLensFlareOcclusionScale", R{ 0.25, 2.0, {} } },
            { "RenderLensFlareFadeTime", R{ 0.1, 1.0, {} } },
            // Lens: chromatic aberration and distortion
            { "RenderChromaticAberrationStrength", R{ 0.0, 1.0, {} } },
            { "RenderChromaticAberrationFalloff", R{ 0.5, 4.0, {} } },
            { "RenderChromaticAberrationAngle", R{ 0.0, 360.0, {} } },
            { "RenderChromaticAberrationAnisotropy", R{ -1.0, 1.0, {} } },
            { "RenderChromaticAberrationOffsetRX", R{ -1.0, 1.0, {} } },
            { "RenderChromaticAberrationOffsetRY", R{ -1.0, 1.0, {} } },
            { "RenderChromaticAberrationOffsetBX", R{ -1.0, 1.0, {} } },
            { "RenderChromaticAberrationOffsetBY", R{ -1.0, 1.0, {} } },
            { "RenderLensDistortionAmount", R{ 0.0, 1.0, {} } },
            { "RenderLensDistortionK1", R{ -0.5, 0.5, {} } },
            { "RenderLensDistortionK2", R{ -0.25, 0.25, {} } },
            { "RenderLensDistortionSqueeze", R{ 0.5, 2.5, {} } },
            { "RenderLensDistortionFit", R{ 0.0, 2.0, { 0, 1, 2 } } },
            { "RenderLensDistortionCenter", R{ -0.5, 0.5, {} } },
            { "RenderLensDistortionTangential", R{ -0.05, 0.05, {} } },
            // Lens: bokeh
            { "RenderBokehHighlightGain", R{ 0.0, 4.0, {} } },
            { "RenderBokehHighlightThreshold", R{ 0.0, 8.0, {} } },
            { "RenderBokehApertureBlades", R{ 0.0, 11.0, { 0, 3, 5, 6, 7, 8, 9, 11 } } },
            { "RenderBokehApertureRotation", R{ 0.0, 360.0, {} } },
            { "RenderBokehApertureCurvature", R{ 0.0, 1.0, {} } },
            { "RenderBokehAnamorphicSqueeze", R{ 0.25, 4.0, {} } },
            { "RenderBokehCatEyeAmount", R{ 0.0, 1.5, {} } },
            { "RenderBokehFringeAmount", R{ 0.0, 1.0, {} } },
            { "RenderBokehFringeNearTint", R{ 0.0, 1.0, {} } },
            { "RenderBokehFringeFarTint", R{ 0.0, 1.0, {} } },
            { "RenderBokehSphericalAberration", R{ -1.0, 1.0, {} } },
            { "RenderBokehFieldStretch", R{ -1.0, 1.0, {} } },
            { "RenderBokehFieldFalloff", R{ 1.0, 4.0, {} } },
            { "RenderBokehComaAsymmetry", R{ 0.0, 1.0, {} } },
            // Lens: cross filter and dirt
            { "RenderCrossFilterStrength", R{ 0.0, 32.0, {} } },
            { "RenderCrossFilterPoints", R{ 2.0, 12.0, { 2, 3, 4, 6, 8, 12 } } },
            { "RenderCrossFilterAngle", R{ 0.0, 360.0, {} } },
            { "RenderCrossFilterLength", R{ 0.25, 2.0, {} } },
            { "RenderCrossFilterFalloff", R{ 0.1, 3.0, {} } },
            { "RenderCrossFilterChromatic", R{ 0.0, 1.0, {} } },
            { "RenderLensDirtStrength", R{ 0.0, 2.0, {} } },
            { "RenderLensDirtBloomResponse", R{ 0.0, 2.0, {} } },
            { "RenderLensDirtFlareResponse", R{ 0.0, 2.0, {} } },
            { "RenderLensDirtGrime", R{ 0.0, 2.0, {} } },
            { "RenderLensDirtMoteScale", R{ 0.5, 2.0, {} } },
            { "RenderLensDirtSmudge", R{ 0.0, 2.0, {} } },
            { "RenderLensDirtScratches", R{ 0.0, 32.0, {} } },
            { "RenderLensDirtSeed", R{ 0.0, 999.0, {} } },
            { "RenderLensDirtToe", R{ 0.6, 4.0, {} } },
            { "RenderLensDirtGain", R{ 0.5, 2.5, {} } },
            // Lens: vignette, grain and sharpening
            { "RenderVignetteAmount", R{ 0.0, 1.0, {} } },
            { "RenderVignetteCenter", R{ -0.5, 0.5, {} } },
            { "RenderVignetteColor", R{ 0.0, 1.0, {} } },
            { "RenderVignetteFeather", R{ 0.2, 4.0, {} } },
            { "RenderVignetteMidColor", R{ 0.0, 1.0, {} } },
            { "RenderVignetteMidPoint", R{ 0.0, 1.0, {} } },
            { "RenderVignetteRadius", R{ 0.25, 1.5, {} } },
            { "RenderVignetteShape", R{ 0.0, 1.0, {} } },
            { "RenderVignetteSoft", R{ 0.0, 1.0, {} } },
            { "RenderFilmGrainAmount", R{ 0.0, 1.0, {} } },
            { "RenderFilmGrainRange", R{ 0.0, 1.0, {} } },
            { "RenderFilmGrainSize", R{ 1.0, 8.0, {} } },
            { "RenderFilmGrainStyle", R{ 0.0, 3.0, { 0, 1, 2, 3 } } },
            { "RenderFilmGrainTint", R{ 0.0, 1.0, {} } },
            { "RenderCASSharpness", R{ 0.0, 1.0, {} } },
        };
        return ranges;
    }

    // A number held to a range: clamped, and `moved` set where it was.
    F64 heldTo(F64 value, const ALLookFormat::SettingRange& range, bool& moved)
    {
        const F64 held = llclamp(value, range.lo, range.hi);
        moved          = moved || held != value;
        return held;
    }

    // A whole number held to a range: to the nearest of its choices where it
    // has them (the lower of two as near), clamped where it does not.
    S32 wholeHeldTo(S32 value, const ALLookFormat::SettingRange& range, bool& moved)
    {
        if (range.choices.empty())
        {
            const S32 held = static_cast<S32>(llclamp(static_cast<F64>(value), range.lo, range.hi));
            moved          = moved || held != value;
            return held;
        }
        S32 nearest = range.choices.front();
        for (const S32 choice : range.choices)
        {
            if (std::llabs(static_cast<S64>(choice) - value) < std::llabs(static_cast<S64>(nearest) - value))
            {
                nearest = choice;
            }
        }
        moved = moved || nearest != value;
        return nearest;
    }
} // namespace

const ALLookFormat::SettingRange* ALLookFormat::settingRange(const std::string& control_name)
{
    const auto& ranges = settingRanges();
    const auto  found  = ranges.find(control_name);
    return found == ranges.end() ? nullptr : &found->second;
}

LLSD ALLookFormat::settingsWithoutDescriptions(const LLSD& look_file_params)
{
    LLSD settings = LLSD::emptyMap();
    if (!look_file_params.isMap())
    {
        return settings;
    }
    for (LLSD::map_const_iterator it = look_file_params.beginMap(); it != look_file_params.endMap(); ++it)
    {
        const LLSD& entry = it->second;
        if (!entry.isMap() || !entry.has("Value"))
        {
            continue;
        }
        LLSD slim = LLSD::emptyMap();
        if (entry.has("Type"))
        {
            slim["Type"] = entry["Type"];
        }
        slim["Value"]        = entry["Value"];
        settings[it->first] = slim;
    }
    return settings;
}

bool ALLookFormat::composeNotecardText(const SharedLook& look, std::string& text)
{
    LLSD body = settingsWithoutDescriptions(look.settings);

    LLSD provenance      = LLSD::emptyMap();
    provenance["Format"] = CURRENT_FORMAT;
    provenance["Name"]   = lookNameFromUntrustedText(look.metadata.name);
    provenance["Author"] = cleanMetadataText(look.metadata.author, PROVENANCE_BYTE_LIMIT);
    provenance["Viewer"] = cleanMetadataText(look.metadata.viewer, PROVENANCE_BYTE_LIMIT);
    provenance["Date"]   = look.metadata.date;
    body[METADATA_KEY_STRING] = provenance;

    // Indented where it fits: that is the form a person opening the notecard
    // can read. A Look with long hand-made curves can run past the limit
    // that way and still fit run together, which the parser reads alike.
    if (!renderLook(body, true, text))
    {
        return false;
    }
    if (text.size() <= NOTECARD_BYTE_LIMIT)
    {
        return true;
    }
    return renderLook(body, false, text) && text.size() <= NOTECARD_BYTE_LIMIT;
}

bool ALLookFormat::parseNotecardText(std::string_view text, SharedLook& look)
{
    // Whatever was put above the header -- a byte-order mark, blank lines,
    // a line of someone's own, with whatever brackets it has -- is not the
    // Look: the Look begins at its <llsd> element.
    const size_t start = text.find("<llsd");
    if (start == std::string_view::npos)
    {
        return false;
    }
    std::istringstream in{ std::string(text.substr(start)) };
    LLSD               body;
    // Quietly: this is a stranger's text, and a notecard that is not a Look
    // is an answer, not an error worth a log line. And no deeper than a Look
    // nests: see XML_DEPTH_LIMIT.
    if (LLSDSerialize::fromXML(body, in, false, XML_DEPTH_LIMIT) == LLSDParser::PARSE_FAILURE || !body.isMap() || !body.has(METADATA_KEY_STRING))
    {
        return false;
    }
    const LLSD& provenance = body[METADATA_KEY_STRING];
    if (!provenance.isMap() || !provenance.has("Format") || !provenance["Format"].isInteger())
    {
        return false;
    }
    look.metadata.format = provenance["Format"].asInteger();
    look.metadata.name   = lookNameFromUntrustedText(provenance["Name"].asString());
    look.metadata.author = cleanMetadataText(provenance["Author"].asString(), PROVENANCE_BYTE_LIMIT);
    look.metadata.viewer = cleanMetadataText(provenance["Viewer"].asString(), PROVENANCE_BYTE_LIMIT);
    look.metadata.date   = provenance["Date"].isDate() ? provenance["Date"].asDate() : LLDate();
    body.erase(METADATA_KEY_STRING);
    look.settings = settingsWithoutDescriptions(body);
    return true;
}

bool ALLookFormat::looksLikeLookNotecard(std::string_view text)
{
    static const std::string marker = "<key>" + METADATA_KEY_STRING + "</key>";
    return text.substr(0, DETECT_WINDOW).find(marker) != std::string_view::npos;
}

bool ALLookFormat::sanitizeLookValue(const std::string& control_name, const std::string& control_type, const LLSD& value, LLSD& sanitized,
                                     bool* adjusted)
{
    // Moved into the setting's range, where it has one and the value was
    // outside it.
    bool                moved = false;
    const SettingRange* range = settingRange(control_name);
    if (adjusted)
    {
        *adjusted = false;
    }
    const auto          said  = [&moved, adjusted]() {
        if (adjusted)
        {
            *adjusted = moved;
        }
        return true;
    };

    if (control_type == "F32")
    {
        F64 real = 0.0;
        if (!finiteSingle(value, real))
        {
            return false;
        }
        sanitized = LLSD::Real(range ? heldTo(real, *range, moved) : real);
        return said();
    }
    if (control_type == "S32" || control_type == "U32")
    {
        // A U32 setting keeps its value as a signed Integer in LLSD
        // (convert_to_llsd<U32> in llcontrol.cpp), so it is taken in that
        // form too.
        if (!wholeNumber(value, sanitized))
        {
            return false;
        }
        if (range)
        {
            sanitized = LLSD::Integer(wholeHeldTo(sanitized.asInteger(), *range, moved));
        }
        return said();
    }
    if (control_type == "Boolean")
    {
        if (value.isBoolean())
        {
            sanitized = LLSD::Boolean(value.asBoolean());
            return true;
        }
        if (value.isString())
        {
            // The words, or the digits a settings file writes one as.
            std::string word = value.asString();
            LLStringUtil::trim(word);
            LLStringUtil::toLower(word);
            if (word == "true" || word == "1")
            {
                sanitized = LLSD::Boolean(true);
                return true;
            }
            if (word == "false" || word == "0")
            {
                sanitized = LLSD::Boolean(false);
                return true;
            }
            return false;
        }
        // settings.xml writes a boolean as the integer 0 or 1, and a
        // checkbox's control has been seen holding the real 1 -- every
        // bundled Look carries RenderGlowNoise so. Exactly 0 or 1 and
        // nothing between: a 0.5 is not a switch anyone set.
        F64 number = 0.0;
        if ((value.isInteger() || value.isReal()) && finiteNumber(value, number) && (number == 0.0 || number == 1.0))
        {
            sanitized = LLSD::Boolean(number == 1.0);
            return true;
        }
        return false;
    }
    if (control_type == "String")
    {
        if (!value.isString())
        {
            return false;
        }
        std::string text = value.asStringRef();
        // Nothing that does not print -- the controls, the line and
        // paragraph separators -- and nothing that is not UTF-8: the parser
        // lets a character reference spell a surrogate, and a file written
        // with one never reads again.
        if (text.size() > LUT_NAME_BYTE_LIMIT || LLStringUtil::containsNonprintable(text))
        {
            return false;
        }
        if (control_name == "RenderColorGradeLUT")
        {
            // The renderer joins this to the LUT folder (setupGradingLUT), so
            // only a bare name may go in, or a shared Look could read any
            // file on the disk. Nothing at all is no LUT, and taken; a name
            // that is not a LUT's -- the wrong kind of file, a device's
            // name -- is refused rather than quietly made into no LUT, so
            // that it is counted among what the Look lost.
            LLStringUtil::trim(text);
            if (!text.empty())
            {
                text = lutFileNameOnly(text);
                if (text.empty())
                {
                    return false;
                }
            }
        }
        sanitized = text;
        return true;
    }
    if (control_type == "Vector3" || control_type == "Color3")
    {
        // Three numbers -- or four with the last ignored: an older colour
        // picker wrote [r, g, b, a] into Color3 settings, LLColor3 reads the
        // first three, and such values are still in saved settings and Looks.
        if (!value.isArray() || value.size() < 3 || value.size() > 4)
        {
            return false;
        }
        LLSD triple = LLSD::emptyArray();
        for (S32 i = 0; i < 3; ++i)
        {
            F64 component = 0.0;
            if (!finiteSingle(value[i], component))
            {
                return false;
            }
            triple.append(LLSD::Real(range ? heldTo(component, *range, moved) : component));
        }
        sanitized = triple;
        return said();
    }
    if (control_type == "LLSD")
    {
        // The tone curves are the only LLSD-typed settings a Look carries.
        // Another LLSD-typed setting needs its own shape judged here, or it
        // is refused: there is no telling a sound value of an unknown shape
        // from garbage.
        std::vector<ALCurveModel::Point> points;
        if (!ALToneCurveSet::pointsFromLLSD(value, points))
        {
            return false;
        }
        sanitized = ALToneCurveSet::pointsToLLSD(points);
        return true;
    }
    // Vector3D, Quaternion, Rect, Color4, and whatever is newer: nothing a
    // Look carries, so nothing is let through.
    return false;
}

const std::vector<std::string>& ALLookFormat::colorLutExtensions()
{
    static const std::vector<std::string> extensions = { "cube", "tga", "png", "jpg", "jpeg", "bmp", "webp" };
    return extensions;
}

std::string ALLookFormat::lutFileNameOnly(const std::string& lut_setting)
{
    const size_t separator = lut_setting.find_last_of("/\\");
    std::string  name      = separator == std::string::npos ? lut_setting : lut_setting.substr(separator + 1);
    LLStringUtil::trim(name);
    // Something before the dot as well as after it: ".cube" is no file
    // (LLDir::getExtension, which the renderer and the picker read the
    // kind by, takes a leading dot as no extension), and neither is "..".
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size())
    {
        return std::string();
    }
    std::string extension = name.substr(dot + 1);
    LLStringUtil::toLower(extension);
    const std::vector<std::string>& allowed = colorLutExtensions();
    if (std::find(allowed.begin(), allowed.end(), extension) == allowed.end())
    {
        return std::string();
    }
    // The renderer opens the LUT at every start: a Look naming a device
    // could hang the viewer on a serial port. No LUT is called that.
    if (isWindowsDeviceName(name))
    {
        return std::string();
    }
    return name;
}

bool ALLookFormat::isWindowsDeviceName(const std::string& file_name)
{
    // Windows before 11 opens "CON", "COM1.png" and "NUL.tar.gz" alike as
    // the device: whatever folder the name is in, whatever follows its
    // first dot, and with spaces at the end of the stem ignored. The stem
    // is compared in ASCII upper case, by hand, so that no locale has a
    // say in it.
    std::string stem = file_name.substr(0, file_name.find('.'));
    LLStringUtil::trimTail(stem);
    for (char& c : stem)
    {
        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - ('a' - 'A'));
        }
    }
    static const char* const DEVICE_NAMES[] = { "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$" };
    for (const char* device : DEVICE_NAMES)
    {
        if (stem == device)
        {
            return true;
        }
    }
    // COM and LPT with a digit -- or with one of the superscript digits
    // U+00B9, U+00B2 and U+00B3, which Windows reserves beside them: in
    // UTF-8 the byte C2 and then B9, B2 or B3.
    if (stem.size() < 4 || (stem.compare(0, 3, "COM") != 0 && stem.compare(0, 3, "LPT") != 0))
    {
        return false;
    }
    const std::string digit = stem.substr(3);
    if (digit.size() == 1)
    {
        return digit[0] >= '0' && digit[0] <= '9';
    }
    if (digit.size() == 2 && static_cast<unsigned char>(digit[0]) == 0xC2)
    {
        const unsigned char second = static_cast<unsigned char>(digit[1]);
        return second == 0xB9 || second == 0xB2 || second == 0xB3;
    }
    return false;
}

namespace
{
    // Whether a character may stand in a Look's name or provenance: one
    // that draws. Out go whatever has no printed form (LLStringOps::
    // isNonprintable: the C0 and C1 controls, DEL, the line and paragraph
    // separators), the '|' an inventory name cannot hold, the '<' and '>'
    // that markup is made of (every notification that shows a name shows it
    // inside <nolink>, so that a URL in it is only text, and a name that
    // could spell </nolink> would end that early), U+100000 and up
    // (the notecard editor shows these as the slots of embedded items,
    // ALNotecardItems, so a name holding one would open with a broken item
    // button in it), the replacement character that malformed bytes decode
    // to, the non-characters, and the format characters that draw nothing
    // yet change how what is around them reads: the bidi embeddings,
    // overrides and isolates, which could make one Look's name read as
    // another's in the list, the zero-width space, the word joiner and its
    // kin, the byte-order mark, the annotation marks and the tags. So do the
    // characters that only ever draw as nothing: the soft hyphen (which
    // shows only where a line breaks, and a name never breaks), the
    // combining grapheme joiner, the Mongolian vowel separator, the Khmer
    // inherent vowels and the Hangul fillers -- a name could be made of
    // them and draw as a blank. The zero-width joiner and non-joiner stay,
    // and so do the bidi marks (U+061C, U+200E, U+200F): a Persian name
    // needs its non-joiner, a family emoji its joiner, and a mark only sets
    // the direction of what stands next to it. Those count for nothing when
    // names are compared (lookNameKey).
    bool drawsInName(llwchar cp)
    {
        if (LLStringOps::isNonprintable(cp) || cp == '|' || cp == '<' || cp == '>' || cp >= 0x100000 || cp == 0xFFFD)
        {
            return false;
        }
        if (cp == 0x200B || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x206F) || cp == 0xFEFF || (cp >= 0xFFF9 && cp <= 0xFFFB) ||
            (cp >= 0xFDD0 && cp <= 0xFDEF) || (cp & 0xFFFE) == 0xFFFE || (cp >= 0xE0000 && cp <= 0xE007F))
        {
            return false;
        }
        if (cp == 0x00AD || cp == 0x034F || cp == 0x115F || cp == 0x1160 || cp == 0x17B4 || cp == 0x17B5 || cp == 0x180E || cp == 0x3164 ||
            cp == 0xFFA0)
        {
            return false;
        }
        return true;
    }

    // The characters a name may keep that draw nothing of their own
    // (drawsInName says why they are kept).
    bool invisibleInName(llwchar cp)
    {
        return cp == 0x200C || cp == 0x200D || cp == 0x061C || cp == 0x200E || cp == 0x200F;
    }
} // namespace

std::string ALLookFormat::lookNameKey(const std::string& name)
{
    std::string key;
    key.reserve(name.size());
    for (size_t i = 0; i < name.size();)
    {
        const LLCodepointAt at = utf8str_decode_at(name, i);
        if (!invisibleInName(at.cp))
        {
            if (at.cp >= 'A' && at.cp <= 'Z')
            {
                key.push_back(static_cast<char>(at.cp - 'A' + 'a'));
            }
            else
            {
                key.append(name, i, at.next - i);
            }
        }
        i = at.next;
    }
    return key;
}

std::string ALLookFormat::cleanMetadataText(const std::string& raw, size_t byte_limit)
{
    // Decoded a character at a time (utf8str_decode_at), so that what is
    // kept is well-formed UTF-8: a stray or missing continuation byte, an
    // overlong form or an encoded surrogate decodes to U+FFFD over one byte
    // and goes. Text that is not well-formed would be written as it is into
    // whatever file the name reaches -- the settings file, as the active
    // Look's -- and no reader would open that file again.
    std::string clean;
    clean.reserve(raw.size());
    for (size_t i = 0; i < raw.size();)
    {
        const LLCodepointAt at = utf8str_decode_at(raw, i);
        if (at.cp == '\t' || at.cp == '\n' || at.cp == '\r')
        {
            // A line break or a tab is a space; a Windows line break is one.
            if (!(at.cp == '\r' && at.next < raw.size() && raw[at.next] == '\n'))
            {
                clean.push_back(' ');
            }
        }
        else if (drawsInName(at.cp))
        {
            clean.append(raw, i, at.next - i);
        }
        i = at.next;
    }
    LLStringUtil::trim(clean);
    // Cut, then trimmed again: the cut can end on a space, and a name with
    // one at the end would read back without it.
    clean = utf8str_truncate(clean, static_cast<S32>(byte_limit));
    LLStringUtil::trimTail(clean);
    return clean;
}

std::string ALLookFormat::lookNameFromUntrustedText(const std::string& raw)
{
    std::string name = cleanMetadataText(raw, LOOK_NAME_BYTE_LIMIT);
    // No dot first: a Look is a file named after it, and the Looks are
    // listed from "*.xml", which leaves out a file whose name begins with
    // one -- such a Look would be saved, applied, and then never seen again
    // to be chosen or deleted.
    name.erase(0, name.find_first_not_of(". "));
    // Nothing worth calling a name: dots, spaces and characters that draw
    // nothing, or a name that Windows would open as a device once it is a
    // file -- a Look is saved as its name and ".xml", and LLURI::escape
    // leaves letters as they are.
    if (lookNameKey(name).find_first_not_of(". ") == std::string::npos || isWindowsDeviceName(name))
    {
        return std::string();
    }
    return name;
}

std::string ALLookFormat::notecardNameForLook(const std::string& look_name)
{
    std::string name = utf8str_truncate(NOTECARD_NAME_PREFIX_STRING + look_name, static_cast<S32>(LOOK_NAME_BYTE_LIMIT));
    LLStringUtil::trimTail(name);
    return name;
}

std::string ALLookFormat::lookNameFromNotecardName(const std::string& notecard_name)
{
    if (notecard_name.compare(0, NOTECARD_NAME_PREFIX_STRING.size(), NOTECARD_NAME_PREFIX_STRING) == 0)
    {
        return lookNameFromUntrustedText(notecard_name.substr(NOTECARD_NAME_PREFIX_STRING.size()));
    }
    return lookNameFromUntrustedText(notecard_name);
}

std::string ALLookFormat::numberedLookName(const std::string& wanted, S32 n, size_t escaped_limit)
{
    if (n < 1 || wanted.empty())
    {
        return std::string();
    }
    const std::string suffix = n == 1 ? std::string() : " (" + std::to_string(n) + ")";
    std::string       base   = utf8str_truncate(wanted, static_cast<S32>(LOOK_NAME_BYTE_LIMIT - suffix.size()));
    if (!suffix.empty())
    {
        LLStringUtil::trimTail(base);
    }
    // Shorter still, a character at a time, until the name as a file name
    // fits: escaped, a byte outside ASCII is three.
    while (!base.empty() && LLURI::escape(base + suffix).size() > escaped_limit)
    {
        base = utf8str_truncate(base, static_cast<S32>(base.size()) - 1);
        LLStringUtil::trimTail(base);
    }
    if (base.empty())
    {
        return std::string();
    }
    return base + suffix;
}
