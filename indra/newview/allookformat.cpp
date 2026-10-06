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

#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>

namespace
{
    const std::string METADATA_KEY_STRING(ALLookFormat::METADATA_KEY);
    const std::string NOTECARD_NAME_PREFIX_STRING(ALLookFormat::NOTECARD_NAME_PREFIX);

    // What the notecard says about itself, for whoever opens it, under the
    // XML declaration where a parser skips a comment. Static text on
    // purpose: a Look's name or author written into it could carry "--",
    // which ends an XML comment early and leaves the rest of the notecard
    // unreadable.
    const char* const NOTE = "\n<!-- Alchemy Lightbox Look. Drop this notecard on the Lightbox window, or open\n"
                             "     it and press Apply Look, to add it to your Looks. Only Lightbox settings\n"
                             "     are read from it, and anything malformed is ignored. -->";

    // The longest a Look's author or viewer is let be said to be.
    constexpr size_t PROVENANCE_BYTE_LIMIT = 128;

    // How far into a text the reserved key has to stand to count. A Look
    // notecard opens with the declaration, the note and that key, well
    // inside this; prose that mentions the key far down is not one.
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
            return numberFromText(value.asString(), out);
        }
        return false;
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

    bool hasControlCharacters(const std::string& text)
    {
        return std::any_of(text.begin(), text.end(), [](char c) {
            const unsigned char byte = static_cast<unsigned char>(c);
            return byte < 0x20 || byte == 0x7F;
        });
    }

    // The note put under the XML declaration, or first where there is none.
    std::string withNote(std::string xml)
    {
        const size_t declared = xml.find("?>");
        xml.insert(declared == std::string::npos ? 0 : declared + 2, NOTE);
        return xml;
    }

    std::string renderLook(const LLSD& body, bool pretty)
    {
        std::ostringstream xml;
        if (pretty)
        {
            LLSDSerialize::toPrettyXML(body, xml);
        }
        else
        {
            LLSDSerialize::toXML(body, xml);
        }
        return withNote(xml.str());
    }
} // namespace

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
    text = renderLook(body, true);
    if (text.size() <= NOTECARD_BYTE_LIMIT)
    {
        return true;
    }
    text = renderLook(body, false);
    return text.size() <= NOTECARD_BYTE_LIMIT;
}

bool ALLookFormat::parseNotecardText(std::string_view text, SharedLook& look)
{
    // Whatever was put above the header -- a byte-order mark, blank lines,
    // a line of someone's own -- is not the Look.
    const size_t start = text.find('<');
    if (start == std::string_view::npos)
    {
        return false;
    }
    std::istringstream in{ std::string(text.substr(start)) };
    LLSD               body;
    // Quietly: this is a stranger's text, and a notecard that is not a Look
    // is an answer, not an error worth a log line.
    if (LLSDSerialize::fromXML(body, in, false) == LLSDParser::PARSE_FAILURE || !body.isMap() || !body.has(METADATA_KEY_STRING))
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

bool ALLookFormat::sanitizeLookValue(const std::string& control_name, const std::string& control_type, const LLSD& value, LLSD& sanitized)
{
    if (control_type == "F32")
    {
        F64 real = 0.0;
        if (!finiteNumber(value, real))
        {
            return false;
        }
        sanitized = LLSD::Real(real);
        return true;
    }
    if (control_type == "S32" || control_type == "U32")
    {
        // A U32 setting keeps its value as a signed Integer in LLSD
        // (convert_to_llsd<U32> in llcontrol.cpp), so it is taken in that
        // form too.
        return wholeNumber(value, sanitized);
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
        std::string text = value.asString();
        if (text.size() > LUT_NAME_BYTE_LIMIT || hasControlCharacters(text))
        {
            return false;
        }
        if (control_name == "RenderColorGradeLUT")
        {
            // The renderer joins this to the LUT folder (setupGradingLUT), so
            // only a bare name may go in, or a shared Look could read any
            // file on the disk.
            text = lutFileNameOnly(text);
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
            if (!finiteNumber(value[i], component))
            {
                return false;
            }
            triple.append(LLSD::Real(component));
        }
        sanitized = triple;
        return true;
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
    if (name.empty() || name == "." || name == "..")
    {
        return std::string();
    }
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= name.size())
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
    return name;
}

std::string ALLookFormat::cleanMetadataText(const std::string& raw, size_t byte_limit)
{
    std::string clean;
    clean.reserve(raw.size());
    for (size_t i = 0; i < raw.size();)
    {
        const unsigned char lead = static_cast<unsigned char>(raw[i]);
        // The length of the UTF-8 sequence this byte leads; a byte that
        // leads nothing -- a stray continuation byte, 0xF5 and up -- is
        // one byte of nothing worth keeping.
        size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
        if (i + length > raw.size())
        {
            // A sequence cut short at the end of the text.
            break;
        }
        bool keep = true;
        if (length == 1)
        {
            if (lead == '\t' || lead == '\n' || lead == '\r')
            {
                clean.push_back(' ');
                keep = false;
            }
            else
            {
                // Below 0x80 is a character of its own; a byte at or above
                // it that led nothing is a stray continuation byte or an
                // invalid lead, and goes.
                keep = lead >= 0x20 && lead < 0x80 && lead != 0x7F && lead != '|';
            }
        }
        else if (length == 2)
        {
            // U+0080 to U+009F, the C1 controls.
            const unsigned char second = static_cast<unsigned char>(raw[i + 1]);
            keep = !(lead == 0xC2 && second >= 0x80 && second <= 0x9F);
        }
        else if (length == 4)
        {
            // U+100000 and up: the notecard editor shows these as the
            // slots of embedded items (ALNotecardItems), so a name holding
            // one would open with a broken item button in it.
            keep = lead != 0xF4;
        }
        if (keep)
        {
            clean.append(raw, i, length);
        }
        i += length;
    }
    LLStringUtil::trim(clean);
    return utf8str_truncate(clean, static_cast<S32>(byte_limit));
}

std::string ALLookFormat::lookNameFromUntrustedText(const std::string& raw)
{
    std::string name = cleanMetadataText(raw, LOOK_NAME_BYTE_LIMIT);
    if (name.find_first_not_of(". ") == std::string::npos)
    {
        return std::string();
    }
    return name;
}

std::string ALLookFormat::notecardNameForLook(const std::string& look_name)
{
    return utf8str_truncate(NOTECARD_NAME_PREFIX_STRING + look_name, static_cast<S32>(LOOK_NAME_BYTE_LIMIT));
}

std::string ALLookFormat::lookNameFromNotecardName(const std::string& notecard_name)
{
    if (notecard_name.compare(0, NOTECARD_NAME_PREFIX_STRING.size(), NOTECARD_NAME_PREFIX_STRING) == 0)
    {
        return lookNameFromUntrustedText(notecard_name.substr(NOTECARD_NAME_PREFIX_STRING.size()));
    }
    return lookNameFromUntrustedText(notecard_name);
}

std::string ALLookFormat::numberedLookName(const std::string& wanted, S32 n)
{
    if (n < 1)
    {
        return std::string();
    }
    if (n == 1)
    {
        return utf8str_truncate(wanted, static_cast<S32>(LOOK_NAME_BYTE_LIMIT));
    }
    const std::string suffix = " (" + std::to_string(n) + ")";
    std::string       base   = utf8str_truncate(wanted, static_cast<S32>(LOOK_NAME_BYTE_LIMIT - suffix.size()));
    LLStringUtil::trimTail(base);
    return base + suffix;
}
