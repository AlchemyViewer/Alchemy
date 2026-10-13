/**
 * @file allookformat.h
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

#pragma once

#include "lldate.h"
#include "llsd.h"
#include "stdtypes.h"

#include <limits>
#include <string>
#include <string_view>
#include <vector>

// A Look -- the Lightbox's aesthetic settings, as LLPresetsManager saves
// them -- carried in a notecard, so that it can be given to anyone the way
// any item is. The text is LLSD XML under a note saying what it is: one
// reserved key with where the Look came from, then every setting as
// {Type, Value}, which is a Look file with its Comment and Persist taken
// off -- a full Look file with them is larger than a notecard holds.
//
// Nothing here reads or writes a setting, so it can be tested on its own.
// LLPresetsManager applies what this reads, through its whitelist alone,
// and asks sanitizeLookValue about every value first.
namespace ALLookFormat
{
    // The one key in the map that is not a setting. Settings are named in
    // [A-Za-z0-9_], so this can never be one; and '@' sorts before every
    // letter, so it is written first, under the note.
    inline constexpr std::string_view METADATA_KEY = "@AlchemyLook";
    // The format this viewer writes. A reader takes a newer one too: a
    // setting it does not know falls outside the whitelist anyway, and is
    // counted rather than refused.
    inline constexpr S32 CURRENT_FORMAT = 1;
    // What a notecard holds, in bytes of text: LLNotecard::MAX_SIZE, which
    // allooknotecard.cpp asserts against so the two cannot drift apart.
    inline constexpr size_t NOTECARD_BYTE_LIMIT = 65536;
    // The longest an inventory item's name may be (DB_INV_ITEM_NAME_STR_LEN),
    // and so the longest a Look's name is let be: a Look file is named after
    // its Look, and LLURI::escape can triple a UTF-8 name on the way.
    // allooknotecard.cpp asserts against the inventory's own, so the two
    // cannot drift apart.
    inline constexpr size_t LOOK_NAME_BYTE_LIMIT = 63;
    // A LUT is named by its file name, and no file system takes a longer one.
    inline constexpr size_t LUT_NAME_BYTE_LIMIT = 255;
    // How deep the XML of a Look -- or any preset file -- may nest before it
    // is refused: <llsd><map><map><array><array><real> is six. The parser
    // keeps its own state on the heap, but what it builds is freed
    // recursively, and a text of sixty kilobytes can nest thousands deep for
    // the sake of nesting.
    inline constexpr S32 XML_DEPTH_LIMIT = 16;
    // What a Look's notecard is called in inventory: this before the Look's
    // name. Code, not a translated string, because the import takes the
    // Look's name back out of a notecard whose body has none.
    inline constexpr std::string_view NOTECARD_NAME_PREFIX = "Lightbox Look: ";

    // Where a Look came from, as the notecard says it.
    struct Metadata
    {
        S32         format = CURRENT_FORMAT;
        std::string name;
        std::string author;
        std::string viewer;
        LLDate      date;
    };

    struct SharedLook
    {
        Metadata metadata;
        // Setting name -> {Type, Value}: a Look file's entries without their
        // Comment and Persist.
        LLSD settings = LLSD::emptyMap();
    };

    // A Look file's map with every entry cut down to its Type and Value.
    // Entries that are not a map holding a Value are dropped, the reserved
    // key among them.
    LLSD settingsWithoutDescriptions(const LLSD& look_file_params);

    // The notecard's text: the note, then the Look as XML -- indented where
    // that fits in a notecard, run together where only that does. False
    // where even that is too long -- `text` then holds the run-together
    // form, so the caller can say by how much -- or where the XML could not
    // be written at all, with `text` empty.
    bool composeNotecardText(const SharedLook& look, std::string& text);

    // The Look a notecard's text holds. Whatever stands before the first
    // "<llsd" is skipped -- a byte-order mark, a blank line, a line typed
    // above it, the note composeNotecardText writes -- and the text must
    // then be an LLSD map holding the
    // reserved key, itself a map with an integer Format. The names in it
    // come back cleaned (cleanMetadataText); the settings come back as they
    // are, for the manager to judge one by one.
    bool parseNotecardText(std::string_view text, SharedLook& look);

    // Whether a text looks like a Look's: the reserved key, early on. A
    // glance for the notecard window to show its offer by, not a parse, and
    // not proof -- parseNotecardText decides.
    bool looksLikeLookNotecard(std::string_view text);

    // Whether a value from outside may be written to a setting of this type
    // (the Type string of settings.xml: "F32", "S32", "U32", "Boolean",
    // "String", "Vector3", "Color3", "LLSD"), and the value as it is to be
    // written. The settings system stores whatever LLSD a widget hands it
    // and coerces on the way out, so what is taken here is what that
    // coercion would make sense of, and no more: a number as a Real, an
    // Integer or a text that is a number and nothing else (a dropdown
    // writes its item's value as a string), finite, and whole for an
    // integer setting; a Boolean, or 0 and 1 as an Integer or a Real, or
    // the words true and false; a String that prints and is well-formed
    // UTF-8, and for RenderColorGradeLUT a bare file name of a kind the
    // renderer decodes or nothing at all; three finite numbers for a
    // Vector3 or a Color3, or four with the last ignored; and, for an LLSD
    // setting, a tone curve as ALToneCurveSet reads one. Anything else is
    // refused, and so is any type not named here. A number is then held to
    // its setting's range (settingRange): clamped into it, or for a setting
    // whose row is a dropdown moved to the nearest value the dropdown has,
    // with @a adjusted, where given, told it was moved. The one place a
    // value from outside is judged.
    bool sanitizeLookValue(const std::string& control_name, const std::string& control_type, const LLSD& value, LLSD& sanitized,
                           bool* adjusted = nullptr);

    // What a setting a Look carries may hold: what its row in the Lightbox
    // offers -- a slider's ends, a colour wheel's or a swatch's per channel,
    // or the values of a dropdown -- so that a Look from outside renders
    // nothing the Lightbox could not show, and that touching the row does
    // not snap back. Each channel of a Vector3 or Color3 is held to the same
    // ends. scripts/content_tools/check_look_ranges.py holds this to the
    // rows themselves, to the list of settings a Look carries and to the
    // bundled Looks.
    struct SettingRange
    {
        F64              lo = 0.0;
        F64              hi = 0.0;
        // Where not empty, the only values a whole-number setting may take.
        std::vector<S32> choices;
    };
    // The range a setting is held to, or null for one that has none: a
    // switch, the LUT's name, the tone curves.
    const SettingRange* settingRange(const std::string& control_name);

    // The file extensions a colour LUT may have: what the Lightbox's picker
    // lists, and what a shared Look may name. The renderer decodes a .cube
    // as a LUT and the rest as images (LLPipeline::setupGradingLUT).
    const std::vector<std::string>& colorLutExtensions();
    // A LUT setting as a bare file name with one of those extensions, or
    // empty for no LUT: whatever stood before the last path separator is
    // taken off, so the name joined to the LUT folder stays inside it; and
    // a device's name (isWindowsDeviceName) is no LUT.
    std::string lutFileNameOnly(const std::string& lut_setting);
    // Whether Windows would open a file of that name as a device, whatever
    // folder it is in: CON, NUL, COM1 and the rest, with whatever follows
    // the first dot ignored ("NUL.tar.gz" is NUL), spaces at the end of the
    // stem ignored, and the superscript digits beside COM and LPT. Nothing
    // may be saved or opened under such a name.
    bool isWindowsDeviceName(const std::string& file_name);

    // A text from outside as it may be shown or used as a name: without
    // control characters, '|' (which inventory names may not hold), '<' and
    // '>' (a notification shows such a text inside <nolink>, which it must
    // not be able to close), or the private-use characters the notecard
    // editor shows as embedded items; trimmed, and cut to `byte_limit` on a
    // character boundary.
    std::string cleanMetadataText(const std::string& raw, size_t byte_limit);
    // A Look's name from a text from outside: cleaned to LOOK_NAME_BYTE_LIMIT,
    // without the dots and spaces it may begin with (the Looks are listed
    // from "*.xml", which leaves out a file whose name begins with a dot),
    // and empty where nothing that draws is left but dots and spaces, or
    // where the name is a device's (isWindowsDeviceName) -- a Look is a file
    // called after it.
    std::string lookNameFromUntrustedText(const std::string& raw);
    // What two names are compared by, to tell whether they name the same
    // Look: ASCII letters in lower case, as the file systems of Windows and
    // macOS match file names; every kind of space one plain space, runs of
    // them one, and none at either end; and without what Unicode calls
    // default-ignorable -- the joiners, the bidi marks, the variation
    // selectors, which a name may keep but which draw nothing of their own.
    // So "Neutral", "Neutral" with a mark or a no-break space after it, and
    // "neutral" are taken for one name, as they read in the list.
    std::string lookNameKey(const std::string& name);
    // What a Look's notecard is called: NOTECARD_NAME_PREFIX and the name,
    // within what an item's name holds.
    std::string notecardNameForLook(const std::string& look_name);
    // A Look's name taken back out of its notecard's name: the prefix off,
    // where it is there, and the rest as lookNameFromUntrustedText has it.
    std::string lookNameFromNotecardName(const std::string& notecard_name);
    // The n-th name tried for a Look called `wanted`: the name itself for 1,
    // then "wanted (2)", "wanted (3)" and so on, each within
    // LOOK_NAME_BYTE_LIMIT with the name cut to make room -- and cut further,
    // where it is given, until the name escaped as a file name
    // (LLURI::escape, which makes three of a byte outside ASCII) is no
    // longer than `escaped_limit`. Empty for n below 1, for a name that is
    // nothing, where the limit leaves no room for any of the name, and where
    // what the cut leaves draws nothing or is a device's name.
    std::string numberedLookName(const std::string& wanted, S32 n, size_t escaped_limit = std::numeric_limits<size_t>::max());
}
