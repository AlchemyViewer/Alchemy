/**
 * @file allookformat_test.cpp
 * @brief Unit tests for a Look written to and read from a notecard's text
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include "../test/lltut.h"

#include "../allookformat.h"

#include "../alcurvemodel.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llstring.h"
#include "lluri.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace tut
{
    struct look_format
    {
        /// A Look file's entry, or a notecard's: Type and Value.
        static LLSD entry(const char* type, const LLSD& value)
        {
            LLSD e     = LLSD::emptyMap();
            e["Type"]  = type;
            e["Value"] = value;
            return e;
        }

        /// A Look file's entry as savePreset writes it, descriptions and all.
        static LLSD fileEntry(const char* type, const LLSD& value)
        {
            LLSD e       = entry(type, value);
            e["Comment"] = "What the setting does, at some length, as the settings file says it";
            e["Persist"] = 1;
            return e;
        }

        static LLSD triple(F64 a, F64 b, F64 c)
        {
            LLSD v = LLSD::emptyArray();
            v.append(a);
            v.append(b);
            v.append(c);
            return v;
        }

        /// A tone curve of `count` points along y = x squared.
        static LLSD curve(S32 count)
        {
            std::vector<ALCurveModel::Point> points;
            for (S32 i = 0; i < count; ++i)
            {
                const F32 x = count > 1 ? static_cast<F32>(i) / static_cast<F32>(count - 1) : 0.f;
                points.push_back({ x, x * x });
            }
            return ALToneCurveSet::pointsToLLSD(points);
        }

        /// A Look the size of the real whitelist and then some: the real
        /// type mix with long names and the longest values a Look carries.
        static LLSD bigLook(S32 curve_points)
        {
            LLSD              look = LLSD::emptyMap();
            const std::string pad(30, 'X');
            for (S32 i = 0; i < 120; ++i)
            {
                look["RenderShared" + pad + std::to_string(i)] = entry("F32", 0.123456789012345678 + i);
            }
            for (S32 i = 0; i < 12; ++i)
            {
                look["RenderTint" + pad + std::to_string(i)] = entry("Color3", triple(0.123456789, 0.987654321, 0.5));
            }
            for (S32 i = 0; i < 10; ++i)
            {
                look["RenderCount" + pad + std::to_string(i)] = entry("S32", 1234567 + i);
            }
            for (S32 i = 0; i < 6; ++i)
            {
                look["RenderVector" + pad + std::to_string(i)] = entry("Vector3", triple(-1.123456789, 2.123456789, 3.5));
            }
            for (S32 i = 0; i < 5; ++i)
            {
                look["RenderSwitch" + pad + std::to_string(i)] = entry("Boolean", true);
            }
            look["RenderUnsigned" + pad]   = entry("U32", 4000000);
            look["RenderColorGradeLUT"]    = entry("String", std::string(250, 'x') + ".cube");
            for (const char* channel : { "Master", "Red", "Green", "Blue" })
            {
                look[std::string("RenderColorGradeCurve") + channel] = entry("LLSD", curve(curve_points));
            }
            return look;
        }

        static ALLookFormat::SharedLook lookNamed(const std::string& name, const LLSD& settings)
        {
            ALLookFormat::SharedLook look;
            look.metadata.name   = name;
            look.metadata.author = "Some Resident";
            look.metadata.viewer = "Alchemy Test 1.2.3.4";
            look.metadata.date   = LLDate("2026-10-06T12:00:00Z");
            look.settings        = settings;
            return look;
        }

        static std::string composed(const ALLookFormat::SharedLook& look)
        {
            std::string text;
            ensure("composes", ALLookFormat::composeNotecardText(look, text));
            return text;
        }

        static S32 newlines(const std::string& text) { return static_cast<S32>(std::count(text.begin(), text.end(), '\n')); }

        /// A value gone through the XML a notecard holds.
        static LLSD throughXml(const std::string& xml)
        {
            std::istringstream in(xml);
            LLSD               out;
            LLSDSerialize::fromXML(out, in, false);
            return out;
        }

        static bool accepts(const char* type, const LLSD& value, LLSD* out = nullptr)
        {
            LLSD clean;
            const bool ok = ALLookFormat::sanitizeLookValue("RenderSomething", type, value, clean);
            if (out)
            {
                *out = clean;
            }
            return ok;
        }
    };

    typedef test_group<look_format> look_format_group;
    typedef look_format_group::object object;
    look_format_group look_format_tests("ALLookFormat");

    // Written, then read: the Look that comes back is the Look that went in.
    template<> template<> void object::test<1>()
    {
        set_test_name("round trip");
        LLSD settings = LLSD::emptyMap();
        settings["RenderExposure"]            = entry("F32", 0.25);
        settings["RenderVignetteColor"]       = entry("Color3", triple(0.1, 0.2, 0.3));
        settings["RenderColorGrade"]          = entry("Boolean", true);
        settings["RenderGlowIterations"]      = entry("S32", 3);
        settings["RenderColorGradeCurveRed"]  = entry("LLSD", curve(5));
        settings["RenderColorGradeLUT"]       = entry("String", "warm.cube");
        const ALLookFormat::SharedLook look = lookNamed("Golden Hour", settings);

        ALLookFormat::SharedLook back;
        ensure("parses", ALLookFormat::parseNotecardText(composed(look), back));
        ensure_equals("format", back.metadata.format, ALLookFormat::CURRENT_FORMAT);
        ensure_equals("name", back.metadata.name, "Golden Hour");
        ensure_equals("author", back.metadata.author, "Some Resident");
        ensure_equals("viewer", back.metadata.viewer, "Alchemy Test 1.2.3.4");
        ensure_equals("date", back.metadata.date.asString(), look.metadata.date.asString());
        ensure("settings", llsd_equals(back.settings, settings));
        ensure("no reserved key among the settings", !back.settings.has(std::string(ALLookFormat::METADATA_KEY)));
    }

    // A Look file carries a Comment and a Persist per setting; the notecard
    // carries neither, which is what makes it fit.
    template<> template<> void object::test<2>()
    {
        set_test_name("descriptions stripped");
        LLSD file                    = LLSD::emptyMap();
        file["RenderExposure"]       = fileEntry("F32", 1.0);
        file["RenderGlowIterations"] = fileEntry("S32", 2);
        file["NotAnEntry"]           = "a bare string";

        const LLSD slim = ALLookFormat::settingsWithoutDescriptions(file);
        ensure_equals("two entries", slim.size(), size_t(2));
        ensure("type kept", slim["RenderExposure"].has("Type"));
        ensure("value kept", slim["RenderExposure"].has("Value"));
        ensure("comment dropped", !slim["RenderExposure"].has("Comment"));
        ensure("persist dropped", !slim["RenderExposure"].has("Persist"));
        ensure("bare string dropped", !slim.has("NotAnEntry"));

        const std::string text = composed(lookNamed("Plain", file));
        ensure("no comment in the text", text.find("<key>Comment</key>") == std::string::npos);
        ensure("no persist in the text", text.find("<key>Persist</key>") == std::string::npos);
    }

    // The whole whitelist, with the longest values it can carry, fits in a
    // notecard: indented with the curves the editor makes, and at least run
    // together with the longest curves a file may hold.
    template<> template<> void object::test<3>()
    {
        set_test_name("size");
        const std::string editor_curves = composed(lookNamed(std::string(63, 'N'), bigLook(ALCurveModel::MAX_POINTS)));
        ensure("fits with the editor's curves", editor_curves.size() <= ALLookFormat::NOTECARD_BYTE_LIMIT);
        ensure("indented when it fits", newlines(editor_curves) > 100);

        const std::string file_curves = composed(lookNamed(std::string(63, 'N'), bigLook(ALCurveModel::MAX_STORED_POINTS)));
        ensure("fits with the longest stored curves", file_curves.size() <= ALLookFormat::NOTECARD_BYTE_LIMIT);

        ALLookFormat::SharedLook back;
        ensure("the run-together form parses", ALLookFormat::parseNotecardText(file_curves, back));
        ensure_equals("and holds every setting", back.settings.size(), bigLook(ALCurveModel::MAX_STORED_POINTS).size());
    }

    // A Look no notecard can hold is refused, and the text left behind says
    // how long it came to.
    template<> template<> void object::test<4>()
    {
        set_test_name("too large");
        LLSD huge = LLSD::emptyMap();
        for (S32 i = 0; i < 3000; ++i)
        {
            huge["RenderPadding" + std::to_string(i)] = entry("F32", 0.123456789 + i);
        }
        std::string text;
        ensure("refused", !ALLookFormat::composeNotecardText(lookNamed("Huge", huge), text));
        ensure("the size is there to report", text.size() > ALLookFormat::NOTECARD_BYTE_LIMIT);
    }

    // What stands above the header is not the Look: a byte-order mark, blank
    // lines, a line typed in, Windows line endings, or no note at all.
    template<> template<> void object::test<5>()
    {
        set_test_name("leading junk and line endings");
        LLSD settings              = LLSD::emptyMap();
        settings["RenderExposure"] = entry("F32", 0.5);
        const std::string text     = composed(lookNamed("Soft Film", settings));
        ensure("opens with the note", text.rfind("<!-- Alchemy Lightbox Look", 0) == 0);
        ensure("the Look follows the note", text.find("<llsd>") != std::string::npos && text.find("<llsd>") > text.find("-->"));

        ALLookFormat::SharedLook back;
        ensure("as written", ALLookFormat::parseNotecardText(text, back));
        ensure("after a byte-order mark", ALLookFormat::parseNotecardText("\xEF\xBB\xBF" + text, back));
        ensure("after blank lines", ALLookFormat::parseNotecardText("\n\n\n" + text, back));
        ensure("after a line of prose", ALLookFormat::parseNotecardText("my favourite look\n" + text, back));
        ensure("after a line of prose with a bracket in it", ALLookFormat::parseNotecardText("I <3 this look\n" + text, back));

        std::string crlf;
        for (char c : text)
        {
            if (c == '\n')
            {
                crlf += '\r';
            }
            crlf += c;
        }
        ensure("with Windows line endings", ALLookFormat::parseNotecardText(crlf, back));

        const size_t note_start = text.find("<!--");
        const size_t note_end   = text.find("-->", note_start);
        ensure("note found", note_start != std::string::npos && note_end != std::string::npos);
        const std::string without_note = text.substr(0, note_start) + text.substr(note_end + 3);
        ensure("without the note", ALLookFormat::parseNotecardText(without_note, back));
        ensure_equals("and still named", back.metadata.name, "Soft Film");
    }

    // The glance the notecard window takes: the reserved key, early.
    template<> template<> void object::test<6>()
    {
        set_test_name("detection");
        LLSD settings              = LLSD::emptyMap();
        settings["RenderExposure"] = entry("F32", 0.5);
        const std::string text     = composed(lookNamed("Soft Film", settings));
        ensure("a Look", ALLookFormat::looksLikeLookNotecard(text));

        const std::string snippets = "<?xml version=\"1.0\" ?>\n<llsd><array><map><key>name</key><string>x</string></map></array></llsd>";
        ensure("not a snippets notecard", !ALLookFormat::looksLikeLookNotecard(snippets));
        ensure("not prose", !ALLookFormat::looksLikeLookNotecard("Dear diary, today I made a Look."));
        ensure("not nothing", !ALLookFormat::looksLikeLookNotecard(""));

        const std::string marker = "<key>" + std::string(ALLookFormat::METADATA_KEY) + "</key>";
        ensure("key at byte 4000 counts", ALLookFormat::looksLikeLookNotecard(std::string(4000, ' ') + marker));
        ensure("key at byte 5000 does not", !ALLookFormat::looksLikeLookNotecard(std::string(5000, ' ') + marker));
    }

    // What is not a Look: not XML, not a map, a map without the reserved
    // key, or one whose Format is not a number.
    template<> template<> void object::test<7>()
    {
        set_test_name("parse rejections");
        ALLookFormat::SharedLook back;
        ensure("prose", !ALLookFormat::parseNotecardText("hello there", back));
        ensure("nothing", !ALLookFormat::parseNotecardText("", back));
        ensure("an array", !ALLookFormat::parseNotecardText("<llsd><array><integer>1</integer></array></llsd>", back));
        ensure("a map without the key",
               !ALLookFormat::parseNotecardText("<llsd><map><key>RenderExposure</key><map><key>Value</key><real>1</real></map></map></llsd>", back));
        const std::string key = std::string(ALLookFormat::METADATA_KEY);
        ensure("a Format that is a string",
               !ALLookFormat::parseNotecardText("<llsd><map><key>" + key + "</key><map><key>Format</key><string>1</string></map></map></llsd>", back));
        ensure("a key that is not a map",
               !ALLookFormat::parseNotecardText("<llsd><map><key>" + key + "</key><integer>1</integer></map></llsd>", back));
        ensure("a Format from the future is read",
               ALLookFormat::parseNotecardText("<llsd><map><key>" + key + "</key><map><key>Format</key><integer>9</integer></map></map></llsd>", back));
        ensure_equals("and says so", back.metadata.format, 9);

        // Character references hold to XML's Char production: a surrogate or
        // a control would ride into the settings file, as the active Look's
        // name, as bytes no reader takes, and the file would never open again.
        const std::string head = "<llsd><map><key>" + key + "</key><map><key>Format</key><integer>1</integer><key>Name</key><string>";
        const std::string tail = "</string></map></map></llsd>";
        ensure("a surrogate reference refuses the text", !ALLookFormat::parseNotecardText(head + "Sunset&#xD800;" + tail, back));
        ensure("a control reference refuses the text", !ALLookFormat::parseNotecardText(head + "Sun&#1;set" + tail, back));
        ensure("a non-character reference refuses the text", !ALLookFormat::parseNotecardText(head + "Sun&#xFFFE;set" + tail, back));
        ensure("a tab reference is read", ALLookFormat::parseNotecardText(head + "Sun&#x9;set" + tail, back));
        ensure_equals("as a space", back.metadata.name, "Sun set");
        ensure("a reference to an emoji is read", ALLookFormat::parseNotecardText(head + "Sun &#x1F305;" + tail, back));
        ensure_equals("and kept", back.metadata.name, "Sun \xF0\x9F\x8C\x85");

        // No deeper than a Look nests: a value wrapped past XML_DEPTH_LIMIT
        // refuses the whole text, before anything is built from it.
        const auto wrapped = [&key](S32 arrays) {
            std::string text = "<llsd><map><key>" + key + "</key><map><key>Format</key><integer>1</integer></map><key>RenderExposure</key><map><key>Value</key>";
            for (S32 i = 0; i < arrays; ++i)
            {
                text += "<array>";
            }
            text += "<real>1</real>";
            for (S32 i = 0; i < arrays; ++i)
            {
                text += "</array>";
            }
            return text + "</map></map></llsd>";
        };
        // <llsd><map><map> are three, the <real> at the bottom is one more.
        ensure("a value nested to the limit is read", ALLookFormat::parseNotecardText(wrapped(ALLookFormat::XML_DEPTH_LIMIT - 4), back));
        ensure("one nested past it is refused", !ALLookFormat::parseNotecardText(wrapped(ALLookFormat::XML_DEPTH_LIMIT - 3), back));
    }

    // A real: finite, as a Real; never a NaN or an infinity, and never a
    // string or a map.
    template<> template<> void object::test<8>()
    {
        set_test_name("F32");
        LLSD out;
        ensure("integer taken", accepts("F32", 7, &out));
        ensure("as a real", out.isReal());
        ensure_approximately_equals("seven", out.asReal(), 7.0, 20);
        ensure("real taken", accepts("F32", 0.25));
        ensure("NaN refused", !accepts("F32", std::numeric_limits<F64>::quiet_NaN()));
        ensure("infinity refused", !accepts("F32", std::numeric_limits<F64>::infinity()));
        ensure("negative infinity refused", !accepts("F32", -std::numeric_limits<F64>::infinity()));
        // LLSD holds doubles and the setting is a float: a double past the
        // float's range is finite here and infinity in the renderer.
        ensure("a double past float range refused", !accepts("F32", 1e39));
        ensure("a negative double past float range refused", !accepts("F32", -1e39));
        ensure("a text past float range refused", !accepts("F32", "1e39"));
        ensure("the largest float taken", accepts("F32", 3.0e38));
        // A dropdown writes its value as a string, so a numeric text is a
        // number; a text that is anything more is not.
        ensure("numeric text taken", accepts("F32", "0.5", &out));
        ensure("as a real", out.isReal());
        ensure_approximately_equals("a half", out.asReal(), 0.5, 20);
        ensure("padded numeric text taken", accepts("F32", " 2 ", &out));
        ensure("exponent text taken", accepts("F32", "1e3", &out));
        ensure("prose refused", !accepts("F32", "half"));
        ensure("trailing junk refused", !accepts("F32", "0.5x"));
        ensure("empty text refused", !accepts("F32", ""));
        ensure("nan text refused", !accepts("F32", "nan"));
        ensure("inf text refused", !accepts("F32", "inf"));
        ensure("hex text refused", !accepts("F32", "0x10"));
        ensure("a boolean refused", !accepts("F32", true));
        ensure("map refused", !accepts("F32", LLSD::emptyMap()));
        ensure("undefined refused", !accepts("F32", LLSD()));

        // Whatever the XML parser makes of "nan", nothing that is not a
        // finite number comes out the other side.
        for (const char* text : { "<llsd><real>nan</real></llsd>", "<llsd><real>inf</real></llsd>", "<llsd><real>-inf</real></llsd>" })
        {
            LLSD       clean;
            const bool ok = ALLookFormat::sanitizeLookValue("RenderSomething", "F32", throughXml(text), clean);
            ensure(std::string("no NaN survives ") + text, !ok || std::isfinite(clean.asReal()));
        }
    }

    // Whole numbers: an Integer as it is, a Real only where it is whole and
    // fits; a U32 the same way, signed as LLSD keeps it.
    template<> template<> void object::test<9>()
    {
        set_test_name("S32 and U32");
        LLSD out;
        ensure("integer taken", accepts("S32", 3, &out));
        ensure("as an integer", out.isInteger());
        ensure("whole real taken", accepts("S32", 7.0, &out));
        ensure("as an integer too", out.isInteger() && out.asInteger() == 7);
        ensure("fraction refused", !accepts("S32", 1.5));
        ensure("too large refused", !accepts("S32", 3e10));
        ensure("NaN refused", !accepts("S32", std::numeric_limits<F64>::quiet_NaN()));
        // What the tonemapper dropdown writes into its S32 setting.
        ensure("numeric text taken", accepts("S32", "5", &out));
        ensure("as the integer", out.isInteger() && out.asInteger() == 5);
        ensure("whole real text taken", accepts("S32", "4.0", &out));
        ensure("as an integer too", out.isInteger() && out.asInteger() == 4);
        ensure("fraction text refused", !accepts("S32", "5.5"));
        ensure("prose refused", !accepts("S32", "five"));
        ensure("a whole exponent text taken", accepts("S32", "1e3", &out));
        ensure("as the integer", out.isInteger() && out.asInteger() == 1000);
        ensure("far below the range refused", !accepts("S32", -3e10));
        ensure("unsigned round trip taken", accepts("U32", -1, &out));
        ensure("kept signed", out.isInteger() && out.asInteger() == -1);
    }

    // A Boolean; the 0 and 1 a settings file writes one as, as an integer or
    // a real; or the words.
    template<> template<> void object::test<10>()
    {
        set_test_name("Boolean");
        LLSD out;
        ensure("true taken", accepts("Boolean", true, &out));
        ensure("as a boolean", out.isBoolean() && out.asBoolean());
        ensure("one taken", accepts("Boolean", 1, &out));
        ensure("as true", out.isBoolean() && out.asBoolean());
        ensure("zero taken", accepts("Boolean", 0, &out));
        ensure("as false", out.isBoolean() && !out.asBoolean());
        ensure("two refused", !accepts("Boolean", 2));
        // What every bundled Look holds RenderGlowNoise as.
        ensure("a real one taken", accepts("Boolean", 1.0, &out));
        ensure("as true", out.isBoolean() && out.asBoolean());
        ensure("a real zero taken", accepts("Boolean", 0.0, &out));
        ensure("as false", out.isBoolean() && !out.asBoolean());
        ensure("a half refused", !accepts("Boolean", 0.5));
        ensure("a real two refused", !accepts("Boolean", 2.0));
        ensure("the word taken", accepts("Boolean", "true", &out));
        ensure("as true", out.isBoolean() && out.asBoolean());
        ensure("the word in capitals taken", accepts("Boolean", "FALSE", &out));
        ensure("as false", out.isBoolean() && !out.asBoolean());
        ensure("the digit taken", accepts("Boolean", "1", &out));
        ensure("as true", out.isBoolean() && out.asBoolean());
        ensure("the other digit taken", accepts("Boolean", "0", &out));
        ensure("as false", out.isBoolean() && !out.asBoolean());
        ensure("a padded word taken", accepts("Boolean", " true ", &out));
        ensure("as true", out.isBoolean() && out.asBoolean());
        ensure("another word refused", !accepts("Boolean", "yes"));
        ensure("an empty text refused", !accepts("Boolean", ""));
        ensure("an array refused", !accepts("Boolean", LLSD::emptyArray()));
    }

    // Three finite numbers, no more and no fewer, as Reals.
    template<> template<> void object::test<11>()
    {
        set_test_name("Vector3 and Color3");
        LLSD out;
        ensure("colour taken", accepts("Color3", triple(0.1, 0.2, 0.3), &out));
        ensure("three reals", out.isArray() && out.size() == 3 && out[0].isReal() && out[2].isReal());
        LLSD mixed = LLSD::emptyArray();
        mixed.append(1);
        mixed.append(2.5);
        mixed.append(3);
        ensure("vector of integers and reals taken", accepts("Vector3", mixed, &out));
        ensure("all reals", out[0].isReal() && out[1].isReal() && out[2].isReal());
        LLSD texts = LLSD::emptyArray();
        texts.append("0.1");
        texts.append("0.2");
        texts.append("0.3");
        ensure("numeric texts taken", accepts("Color3", texts, &out));
        ensure("as reals", out[0].isReal() && out[1].isReal() && out[2].isReal());

        LLSD two = LLSD::emptyArray();
        two.append(0.1);
        two.append(0.2);
        ensure("two refused", !accepts("Color3", two));
        // What an older colour picker wrote into a Color3: [r, g, b, a].
        LLSD four = triple(0.1, 0.2, 0.3);
        four.append(1.0);
        ensure("four taken", accepts("Color3", four, &out));
        ensure("cut to three", out.isArray() && out.size() == 3);
        ensure_approximately_equals("the third kept", out[2].asReal(), 0.3, 20);
        LLSD fourth_junk = triple(0.1, 0.2, 0.3);
        fourth_junk.append("alpha");
        ensure("a fourth element is not even looked at", accepts("Color3", fourth_junk, &out));
        ensure("and is gone", out.size() == 3);
        LLSD five = four;
        five.append(1.0);
        ensure("five refused", !accepts("Color3", five));
        LLSD words = LLSD::emptyArray();
        words.append("r");
        words.append("g");
        words.append("b");
        ensure("words refused", !accepts("Color3", words));
        ensure("NaN refused", !accepts("Vector3", triple(0.0, std::numeric_limits<F64>::quiet_NaN(), 0.0)));
        ensure("a component past float range refused", !accepts("Vector3", triple(0.0, 1e39, 0.0)));
        ensure("a colour component past float range refused", !accepts("Color3", triple(-1e39, 0.0, 0.0)));
        ensure("a map refused", !accepts("Vector3", LLSD::emptyMap()));
        ensure("a number refused", !accepts("Vector3", 1.0));
    }

    // An LLSD setting is a tone curve, as ALToneCurveSet reads one, or nothing.
    template<> template<> void object::test<12>()
    {
        set_test_name("tone curve");
        LLSD out;
        ensure("three points taken", accepts("LLSD", curve(3), &out));
        ensure("as an array of three", out.isArray() && out.size() == 3);
        ensure("the most a file may hold taken", accepts("LLSD", curve(ALCurveModel::MAX_STORED_POINTS)));
        ensure("one point refused", !accepts("LLSD", curve(1)));
        ensure("one too many refused", !accepts("LLSD", curve(ALCurveModel::MAX_STORED_POINTS + 1)));
        // A point past float range is finite as the double LLSD holds and
        // infinity as the float a point is; the fuzz below found this one.
        LLSD overflow = LLSD::emptyArray();
        LLSD beyond   = LLSD::emptyArray(); // not "far": a Windows macro
        beyond.append(1e39);
        beyond.append(0.0);
        overflow.append(beyond);
        overflow.append(triple(1.0, 1.0, 0.0));
        ensure("a point past float range refused", !accepts("LLSD", overflow));
        // A malformed point among sound ones is dropped by the curve model
        // (pointsFromLLSD skips it), as it always was for a file: the curve
        // is taken with what is left, as long as that is still a curve.
        LLSD pair_a = LLSD::emptyArray();
        pair_a.append(0.0);
        pair_a.append(0.0);
        LLSD pair_bad = LLSD::emptyArray();
        pair_bad.append(0.5);
        pair_bad.append("x");
        LLSD pair_c = LLSD::emptyArray();
        pair_c.append(1.0);
        pair_c.append(1.0);
        LLSD mixed_curve = LLSD::emptyArray();
        mixed_curve.append(pair_a);
        mixed_curve.append(pair_bad);
        mixed_curve.append(pair_c);
        ensure("a malformed point is dropped and the rest taken", accepts("LLSD", mixed_curve, &out));
        ensure_equals("two points left", out.size(), size_t(2));
        LLSD too_few = LLSD::emptyArray();
        too_few.append(pair_a);
        too_few.append(pair_bad);
        ensure("but not where too few are left", !accepts("LLSD", too_few));
        ensure("a string refused", !accepts("LLSD", "[[0,0],[1,1]]"));
        ensure("a map refused", !accepts("LLSD", LLSD::emptyMap()));
    }

    // A LUT is a bare file name of a kind the renderer decodes, or no LUT.
    template<> template<> void object::test<13>()
    {
        set_test_name("LUT name");
        ensure_equals("plain", ALLookFormat::lutFileNameOnly("warm.cube"), "warm.cube");
        ensure_equals("upper case extension", ALLookFormat::lutFileNameOnly("WARM.CUBE"), "WARM.CUBE");
        ensure_equals("climbing out", ALLookFormat::lutFileNameOnly("../../x.cube"), "x.cube");
        ensure_equals("a Windows path", ALLookFormat::lutFileNameOnly("C:\\a\\b.png"), "b.png");
        ensure_equals("dot dot", ALLookFormat::lutFileNameOnly(".."), "");
        ensure_equals("dot", ALLookFormat::lutFileNameOnly("."), "");
        ensure_equals("nothing", ALLookFormat::lutFileNameOnly(""), "");
        ensure_equals("a folder", ALLookFormat::lutFileNameOnly("luts/"), "");
        ensure_equals("not an image", ALLookFormat::lutFileNameOnly("notes.txt"), "");
        ensure_equals("no extension", ALLookFormat::lutFileNameOnly("warm"), "");
        ensure_equals("a bare dot at the end", ALLookFormat::lutFileNameOnly("warm."), "");
        ensure_equals("nothing before the dot is no file", ALLookFormat::lutFileNameOnly(".cube"), "");
        // Names Windows before 11 opens as devices, whatever folder they are
        // in and whatever follows the first dot.
        for (const char* device : { "CON.cube", "con.png", "Nul.tga", "COM1.cube", "lpt9.jpg", "AUX.webp", "PRN.bmp", "CONOUT$.cube", "com1.warm.cube",
                                    "NUL.tar.cube", "con .cube", "COM\xC2\xB9.cube", "LPT\xC2\xB3.png" })
        {
            ensure_equals(std::string("a device name is no LUT: ") + device, ALLookFormat::lutFileNameOnly(device), "");
        }
        ensure_equals("a name that merely starts like one is a LUT", ALLookFormat::lutFileNameOnly("console.cube"), "console.cube");
        ensure_equals("and one with a digit too many", ALLookFormat::lutFileNameOnly("COM10.cube"), "COM10.cube");
        ensure("a device name is a device name", ALLookFormat::isWindowsDeviceName("NUL") && ALLookFormat::isWindowsDeviceName("nul.foo.bar") &&
                                                     ALLookFormat::isWindowsDeviceName("COM1 ") && ALLookFormat::isWindowsDeviceName("lpt0"));
        ensure("and other names are not", !ALLookFormat::isWindowsDeviceName("NULL") && !ALLookFormat::isWindowsDeviceName("COM") &&
                                              !ALLookFormat::isWindowsDeviceName("COM1x") && !ALLookFormat::isWindowsDeviceName(""));

        LLSD out;
        ensure("the setting goes through the same cut", ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", "..\\..\\x.cube", out));
        ensure_equals("and comes out bare", out.asString(), "x.cube");
        ensure("no LUT is taken", ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", "", out));
        ensure_equals("as nothing", out.asString(), "");
        ensure("spaces alone are no LUT", ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", "   ", out));
        ensure_equals("and nothing too", out.asString(), "");
        // A name that is no LUT's is refused, not quietly made into no LUT,
        // so that the import counts it among what the Look lost.
        ensure("the wrong kind of file refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", "notes.txt", out));
        ensure("a device refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", "CON.cube", out));
        ensure("a folder refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", "luts/", out));
        ensure("a control character refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", std::string("warm\n.cube"), out));
        ensure("a C1 control refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", std::string("warm\xC2\x85.cube"), out));
        ensure("a line separator refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", std::string("warm\xE2\x80\xA8.cube"), out));
        ensure("an encoded surrogate refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", std::string("warm\xED\xA0\x80.cube"), out));
        ensure("an overlong form refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", std::string("warm\xC0\x80.cube"), out));
        ensure("too long refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", std::string(300, 'x') + ".cube", out));
        ensure("a number refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", 5, out));
        ensure("another string setting is not cut", ALLookFormat::sanitizeLookValue("RenderSomeName", "String", "a/b", out));
        ensure_equals("and keeps its slash", out.asString(), "a/b");
        ensure("but is held to print too", !ALLookFormat::sanitizeLookValue("RenderSomeName", "String", std::string("a\x7F" "b"), out));
    }

    // Types a Look does not carry are refused outright.
    template<> template<> void object::test<14>()
    {
        set_test_name("unsupported types");
        for (const char* type : { "Rect", "Color4", "Quaternion", "Vector3D", "Nonsense", "" })
        {
            ensure(std::string("refused: ") + type, !accepts(type, 1));
        }
    }

    // Names from outside: cut on a character boundary, without what an
    // inventory name or a notecard cannot hold, and empty where nothing is
    // left worth calling a name.
    template<> template<> void object::test<15>()
    {
        set_test_name("names");
        // Thirty-four hiragana characters, three bytes each: 102 bytes.
        std::string cjk;
        for (S32 i = 0; i < 34; ++i)
        {
            cjk += "\xE3\x81\x82";
        }
        const std::string cut = ALLookFormat::lookNameFromUntrustedText(cjk);
        ensure("within the limit", cut.size() <= ALLookFormat::LOOK_NAME_BYTE_LIMIT);
        ensure("on a character boundary", cut.size() % 3 == 0 && cut.size() > 0);
        ensure("a prefix of the name", cjk.compare(0, cut.size(), cut) == 0);

        ensure_equals("a bar goes", ALLookFormat::lookNameFromUntrustedText("Soft|Film"), "SoftFilm");
        ensure_equals("a newline is a space", ALLookFormat::lookNameFromUntrustedText("Soft\nFilm"), "Soft Film");
        ensure_equals("a tab is a space", ALLookFormat::lookNameFromUntrustedText("Soft\tFilm"), "Soft Film");
        // The literals are split after each escape: a hex escape swallows
        // every hex digit that follows it, and 'F' is one.
        ensure_equals("a control character goes", ALLookFormat::lookNameFromUntrustedText("Soft\x01" "Film"), "SoftFilm");
        ensure_equals("a C1 control goes", ALLookFormat::lookNameFromUntrustedText("Soft\xC2\x85" "Film"), "SoftFilm");
        ensure_equals("an embedded item slot goes", ALLookFormat::lookNameFromUntrustedText("Soft\xF4\x80\x80\x80" "Film"), "SoftFilm");
        ensure_equals("a stray continuation byte goes", ALLookFormat::lookNameFromUntrustedText("Soft\x80" "Film"), "SoftFilm");
        ensure_equals("a cut-short sequence goes", ALLookFormat::lookNameFromUntrustedText("Film\xE3\x81"), "Film");
        // Not well-formed UTF-8 either, each decoded to nothing: an overlong
        // form, an encoded surrogate, a lead whose continuation never comes.
        ensure_equals("an overlong form goes", ALLookFormat::lookNameFromUntrustedText("Soft\xC0\x80" "Film"), "SoftFilm");
        ensure_equals("an encoded surrogate goes", ALLookFormat::lookNameFromUntrustedText("Sunset\xED\xA0\x80"), "Sunset");
        ensure_equals("a lead without its continuation goes and the rest stays", ALLookFormat::lookNameFromUntrustedText("A\xE2" "AAB"), "AAAB");
        ensure("what is kept is well-formed", utf8str_is_valid(ALLookFormat::lookNameFromUntrustedText("\xE2\x82" "x\xF0\x9F\x98\x80" "\xC3")));
        // Format characters that draw nothing and change how the rest reads.
        ensure_equals("a bidi override goes", ALLookFormat::lookNameFromUntrustedText("Soft\xE2\x80\xAE" "Film"), "SoftFilm");
        ensure_equals("a zero-width space goes", ALLookFormat::lookNameFromUntrustedText("Soft\xE2\x80\x8B" "Film"), "SoftFilm");
        ensure_equals("a byte-order mark goes", ALLookFormat::lookNameFromUntrustedText("\xEF\xBB\xBF" "Soft Film"), "Soft Film");
        ensure_equals("a replacement character goes", ALLookFormat::lookNameFromUntrustedText("Soft\xEF\xBF\xBD" "Film"), "SoftFilm");
        ensure_equals("an emoji stays", ALLookFormat::lookNameFromUntrustedText("Soft \xF0\x9F\x8C\x85"), "Soft \xF0\x9F\x8C\x85");
        ensure_equals("trimmed", ALLookFormat::lookNameFromUntrustedText("  Soft Film  "), "Soft Film");
        ensure_equals("accents stay", ALLookFormat::lookNameFromUntrustedText("Caf\xC3\xA9"), "Caf\xC3\xA9");
        // Format characters that carry their weight stay: a joiner holds a
        // family emoji together, a non-joiner shapes Persian, and a mark
        // only sets the direction of what stands next to it.
        ensure_equals("a zero-width joiner stays", ALLookFormat::lookNameFromUntrustedText("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"),
                      "\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7");
        ensure_equals("a zero-width non-joiner stays", ALLookFormat::lookNameFromUntrustedText("\xD9\x85\xDB\x8C\xE2\x80\x8C\xD8\xAE"),
                      "\xD9\x85\xDB\x8C\xE2\x80\x8C\xD8\xAE");
        ensure_equals("a left-to-right mark stays", ALLookFormat::lookNameFromUntrustedText("Soft\xE2\x80\x8E" "Film"), "Soft\xE2\x80\x8E" "Film");
        ensure_equals("a line separator goes", ALLookFormat::lookNameFromUntrustedText("Soft\xE2\x80\xA8" "Film"), "SoftFilm");
        ensure_equals("a Windows line break is one space", ALLookFormat::lookNameFromUntrustedText("Soft\r\nFilm"), "Soft Film");
        // Markup's brackets: a notification shows a name inside <nolink>, so
        // that a URL in it is only text, and a name that could close that
        // early would put the rest of itself back to being a link.
        ensure_equals("angle brackets go", ALLookFormat::lookNameFromUntrustedText("Soft <b>Film</b>"), "Soft bFilm/b");
        ensure_equals("so no name can close a nolink", ALLookFormat::lookNameFromUntrustedText("x</nolink>[https://example.com Look]"),
                      "x/nolink[https://example.com Look]");
        ensure_equals("nor open an icon", ALLookFormat::cleanMetadataText("<icon>Inv_Notecard</icon>", ALLookFormat::LOOK_NAME_BYTE_LIMIT),
                      "iconInv_Notecard/icon");
        // The cut can land after a space; the name must not end on it.
        ensure_equals("no space left at the end of a cut", ALLookFormat::lookNameFromUntrustedText(std::string(62, 'a') + " b"), std::string(62, 'a'));
        ensure_equals("the limit itself is whole", ALLookFormat::lookNameFromUntrustedText(std::string(63, 'a')).size(), ALLookFormat::LOOK_NAME_BYTE_LIMIT);
        ensure_equals("one past it is cut", ALLookFormat::lookNameFromUntrustedText(std::string(64, 'a')).size(), ALLookFormat::LOOK_NAME_BYTE_LIMIT);
        // A Look is a file called after it, and Windows opens some names as
        // devices whatever folder they are in.
        ensure_equals("a device name is nothing", ALLookFormat::lookNameFromUntrustedText("COM1"), "");
        ensure_equals("with an extension too", ALLookFormat::lookNameFromUntrustedText("NUL.foo"), "");
        ensure_equals("in any case", ALLookFormat::lookNameFromUntrustedText("con"), "");
        ensure_equals("a name that merely starts like one stays", ALLookFormat::lookNameFromUntrustedText("console"), "console");
        ensure_equals("spaces alone are nothing", ALLookFormat::lookNameFromUntrustedText("   "), "");
        ensure_equals("dots alone are nothing", ALLookFormat::lookNameFromUntrustedText(". . ."), "");
        ensure_equals("nothing is nothing", ALLookFormat::lookNameFromUntrustedText(""), "");
        // The Looks are listed from "*.xml", which leaves out a file whose
        // name begins with a dot: such a Look could be saved, and never
        // chosen or deleted again.
        ensure_equals("no dot first", ALLookFormat::lookNameFromUntrustedText(".Neutral"), "Neutral");
        ensure_equals("nor dots and spaces first", ALLookFormat::lookNameFromUntrustedText(". . .Neutral"), "Neutral");
        ensure_equals("a dot further in stays", ALLookFormat::lookNameFromUntrustedText("v1.2"), "v1.2");
        // What draws as nothing at all is not a name, nor part of one.
        ensure_equals("a soft hyphen goes", ALLookFormat::lookNameFromUntrustedText("Soft\xC2\xAD" "Film"), "SoftFilm");
        ensure_equals("a Hangul filler goes", ALLookFormat::lookNameFromUntrustedText("Soft\xE3\x85\xA4" "Film"), "SoftFilm");
        ensure_equals("a combining grapheme joiner goes", ALLookFormat::lookNameFromUntrustedText("Soft\xCD\x8F" "Film"), "SoftFilm");
        ensure_equals("joiners alone are nothing", ALLookFormat::lookNameFromUntrustedText("\xE2\x80\x8D\xE2\x80\x8D"), "");
        ensure_equals("marks and dots alone are nothing", ALLookFormat::lookNameFromUntrustedText("\xE2\x80\x8E.\xE2\x80\x8F"), "");
        // Compared, a name that reads as another is that other.
        ensure_equals("a key is in lower case", ALLookFormat::lookNameKey("Soft Film"), "soft film");
        ensure_equals("a mark counts for nothing in a key", ALLookFormat::lookNameKey("Neutral\xE2\x80\x8E"), ALLookFormat::lookNameKey("neutral"));
        ensure_equals("nor does a joiner", ALLookFormat::lookNameKey("Soft\xE2\x80\x8D" "Film"), "softfilm");
        ensure_equals("letters outside ASCII are left as they are", ALLookFormat::lookNameKey("\xC3\x89t\xC3\xA9"), "\xC3\x89t\xC3\xA9");

        const std::string notecard = ALLookFormat::notecardNameForLook(std::string(70, 'L'));
        ensure("the notecard's name fits an item", notecard.size() <= ALLookFormat::LOOK_NAME_BYTE_LIMIT);
        ensure("and opens with the prefix", notecard.rfind(std::string(ALLookFormat::NOTECARD_NAME_PREFIX), 0) == 0);
        ensure_equals("a short one is whole", ALLookFormat::notecardNameForLook("Soft Film"), "Lightbox Look: Soft Film");
        ensure_equals("the name comes back out", ALLookFormat::lookNameFromNotecardName("Lightbox Look: Soft Film"), "Soft Film");
        ensure_equals("a notecard named otherwise is taken whole", ALLookFormat::lookNameFromNotecardName("My Note"), "My Note");
        ensure_equals("a prefix alone is nothing", ALLookFormat::lookNameFromNotecardName("Lightbox Look: "), "");
        ensure_equals("a notecard named after a device gives no name back", ALLookFormat::lookNameFromNotecardName("Lightbox Look: NUL"), "");
        const std::string cut_name = ALLookFormat::notecardNameForLook(std::string(47, 'a') + " bb");
        ensure("a notecard's name does not end on the space the cut left", !cut_name.empty() && cut_name.back() != ' ');
    }

    // The note is static text: a name that would end a comment cannot. The
    // '>' never reaches a name (test 15); the "--" does, and neither belongs
    // in a comment.
    template<> template<> void object::test<16>()
    {
        set_test_name("a name that would end the note");
        LLSD settings              = LLSD::emptyMap();
        settings["RenderExposure"] = entry("F32", 0.5);
        const std::string name     = "Soft --> Film -- take 2";
        const std::string text     = composed(lookNamed(name, settings));
        const size_t      note     = text.find("<!--");
        const size_t      note_end = text.find("-->", note);
        ensure("the note is closed before the Look begins", note != std::string::npos && note_end != std::string::npos && note_end < text.find("<llsd"));
        ALLookFormat::SharedLook back;
        ensure("and the Look reads", ALLookFormat::parseNotecardText(text, back));
        ensure_equals("with its name, less the '>'", back.metadata.name, "Soft -- Film -- take 2");
    }

    // The names tried for a Look whose name is taken: the name, then numbered,
    // each within what an item's name holds.
    template<> template<> void object::test<17>()
    {
        set_test_name("numbered names");
        ensure_equals("first", ALLookFormat::numberedLookName("Sunset", 1), "Sunset");
        ensure_equals("second", ALLookFormat::numberedLookName("Sunset", 2), "Sunset (2)");
        ensure_equals("tenth", ALLookFormat::numberedLookName("Sunset", 10), "Sunset (10)");
        ensure_equals("none before the first", ALLookFormat::numberedLookName("Sunset", 0), "");

        const std::string longest = std::string(63, 'L');
        const std::string second  = ALLookFormat::numberedLookName(longest, 2);
        ensure("a long name makes room", second.size() <= ALLookFormat::LOOK_NAME_BYTE_LIMIT);
        ensure("for its number", second.size() >= 4 && second.compare(second.size() - 4, 4, " (2)") == 0);
        ensure_equals("a long name is cut even first", ALLookFormat::numberedLookName(std::string(70, 'L'), 1).size(), ALLookFormat::LOOK_NAME_BYTE_LIMIT);
        ensure_equals("no name numbers to nothing", ALLookFormat::numberedLookName("", 2), "");
        // Twenty-one hiragana, 63 bytes: the room for the number is made on
        // a character boundary.
        std::string cjk;
        for (S32 i = 0; i < 21; ++i)
        {
            cjk += "\xE3\x81\x82";
        }
        const std::string cjk_second = ALLookFormat::numberedLookName(cjk, 2);
        ensure("a wide name makes room on a boundary", utf8str_is_valid(cjk_second) && cjk_second.size() <= ALLookFormat::LOOK_NAME_BYTE_LIMIT);
        ensure("and ends with its number", cjk_second.size() >= 4 && cjk_second.compare(cjk_second.size() - 4, 4, " (2)") == 0);

        // Within a length escaped as a file name, as a path that Windows can
        // open needs: a byte outside ASCII escapes to three.
        const std::string fitted = ALLookFormat::numberedLookName(cjk, 1, 30);
        ensure("cut to fit escaped", LLURI::escape(fitted).size() <= 30);
        ensure_equals("whole characters only", fitted, std::string("\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82"));
        const std::string fitted_second = ALLookFormat::numberedLookName(cjk, 2, 30);
        ensure("numbered and cut to fit escaped", LLURI::escape(fitted_second).size() <= 30);
        ensure("and still ends with its number", fitted_second.size() >= 4 && fitted_second.compare(fitted_second.size() - 4, 4, " (2)") == 0);
        ensure_equals("an ASCII name fits as it is", ALLookFormat::numberedLookName("Sunset", 1, 6), "Sunset");
        ensure_equals("no room for any of it is nothing", ALLookFormat::numberedLookName("Sunset", 2, 8), "");
    }

    // The shapes a Look written by the viewer actually carries, as the first
    // shared Look showed: the settings system keeps whatever LLSD a widget
    // handed it, so the tonemapper dropdown's S32 arrives as the string "5"
    // and a checkbox's Boolean as the real 1. Both must be taken, or every
    // Look -- the bundled ones included -- would lose those settings.
    template<> template<> void object::test<18>()
    {
        set_test_name("values as the viewer writes them");
        const std::string key = std::string(ALLookFormat::METADATA_KEY);
        const std::string text =
            "<llsd><map>"
            "<key>" + key + "</key><map><key>Format</key><integer>1</integer><key>Name</key><string>Vibrance</string></map>"
            "<key>AlchemyRenderTonemapType</key><map><key>Type</key><string>S32</string><key>Value</key><string>5</string></map>"
            "<key>RenderGlowNoise</key><map><key>Type</key><string>Boolean</string><key>Value</key><real>1</real></map>"
            "<key>RenderDynamicExposureEnabled</key><map><key>Type</key><string>Boolean</string><key>Value</key><integer>1</integer></map>"
            "<key>RenderColorGrade</key><map><key>Type</key><string>Boolean</string><key>Value</key><boolean>1</boolean></map>"
            "</map></llsd>";
        ALLookFormat::SharedLook look;
        ensure("parses", ALLookFormat::parseNotecardText(text, look));
        ensure_equals("four settings", look.settings.size(), size_t(4));

        LLSD clean;
        ensure("the tonemapper's string is a number",
               ALLookFormat::sanitizeLookValue("AlchemyRenderTonemapType", "S32", look.settings["AlchemyRenderTonemapType"]["Value"], clean));
        ensure("and comes out as the integer", clean.isInteger() && clean.asInteger() == 5);
        ensure("the noise switch's real is a switch", ALLookFormat::sanitizeLookValue("RenderGlowNoise", "Boolean", look.settings["RenderGlowNoise"]["Value"], clean));
        ensure("and is on", clean.isBoolean() && clean.asBoolean());
        ensure("an integer switch is a switch",
               ALLookFormat::sanitizeLookValue("RenderDynamicExposureEnabled", "Boolean", look.settings["RenderDynamicExposureEnabled"]["Value"], clean));
        ensure("and is on", clean.isBoolean() && clean.asBoolean());
        ensure("a boolean switch is a switch", ALLookFormat::sanitizeLookValue("RenderColorGrade", "Boolean", look.settings["RenderColorGrade"]["Value"], clean));
        ensure("and is on", clean.isBoolean() && clean.asBoolean());
    }

    // The reader under hostile text: a notecard is a stranger's bytes, so the
    // reader is fed mutations of real Looks and the deepest nesting a
    // notecard can hold, and what comes out is held to the shapes the
    // sanitizer promises. Deterministic, so a failure can be re-run.
    namespace
    {
        struct FuzzRng
        {
            U64 state;
            explicit FuzzRng(U64 seed) : state(seed ? seed : 0x9E3779B97F4A7C15ull) {}
            U64 next()
            {
                state ^= state << 13;
                state ^= state >> 7;
                state ^= state << 17;
                return state;
            }
            size_t below(size_t n) { return n ? static_cast<size_t>(next() % n) : 0; }
        };

        void exerciseReader(const std::string& text, size_t& parsed_count)
        {
            ALLookFormat::looksLikeLookNotecard(text);
            ALLookFormat::SharedLook look;
            if (ALLookFormat::parseNotecardText(text, look))
            {
                ++parsed_count;
                static const char* const types[] = { "F32", "S32", "U32", "Boolean", "String", "Vector3", "Color3", "LLSD", "Rect" };
                for (LLSD::map_const_iterator it = look.settings.beginMap(); it != look.settings.endMap(); ++it)
                {
                    const LLSD& value = it->second["Value"];
                    for (const char* type_name : types)
                    {
                        const std::string type(type_name);
                        LLSD              clean;
                        if (!ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", type, value, clean))
                        {
                            continue;
                        }
                        // A value that passed must be of the promised shape.
                        if (type == "F32")
                        {
                            ensure("F32 is a finite real", clean.isReal() && std::isfinite(clean.asReal()));
                        }
                        else if (type == "S32" || type == "U32")
                        {
                            ensure("S32 is an integer", clean.isInteger());
                        }
                        else if (type == "Boolean")
                        {
                            ensure("Boolean is a boolean", clean.isBoolean());
                        }
                        else if (type == "String")
                        {
                            ensure("String is short", clean.isString() && clean.asString().size() <= 255);
                            const std::string s = clean.asString();
                            ensure("LUT has no separator", s.find_first_of("/\\") == std::string::npos);
                            ensure("LUT is a file name or nothing", s.empty() || ALLookFormat::lutFileNameOnly(s) == s);
                        }
                        else if (type == "Vector3" || type == "Color3")
                        {
                            ensure("triple of finite reals", clean.isArray() && clean.size() == 3 && clean[0].isReal() && clean[1].isReal() && clean[2].isReal() &&
                                                                 std::isfinite(clean[0].asReal()) && std::isfinite(clean[1].asReal()) && std::isfinite(clean[2].asReal()));
                        }
                        else if (type == "LLSD")
                        {
                            std::vector<ALCurveModel::Point> pts;
                            // What the sanitizer wrote must read back as a
                            // curve: 1e39 once passed as a double and came
                            // out as infinity.
                            ensure("curve re-reads", ALToneCurveSet::pointsFromLLSD(clean, pts));
                            for (const ALCurveModel::Point& p : pts)
                            {
                                ensure("curve point finite", std::isfinite(p.mX) && std::isfinite(p.mY));
                            }
                        }
                        else
                        {
                            ensure("Rect never passes", false);
                        }
                    }
                    // Any other String setting takes the same text uncut.
                    LLSD plain;
                    if (ALLookFormat::sanitizeLookValue("RenderSomething", "String", value, plain))
                    {
                        ensure("a plain String is short and prints",
                               plain.isString() && plain.asString().size() <= 255 && !LLStringUtil::containsNonprintable(plain.asString()));
                    }
                }
                ensure("name within limit", look.metadata.name.size() <= ALLookFormat::LOOK_NAME_BYTE_LIMIT);
                ensure("name without markup's brackets", look.metadata.name.find_first_of("<>") == std::string::npos);
                ensure("numbered within limit", ALLookFormat::numberedLookName(look.metadata.name, 2).size() <= ALLookFormat::LOOK_NAME_BYTE_LIMIT);
                ensure("numbered 999 within limit", ALLookFormat::numberedLookName(look.metadata.name, 999).size() <= ALLookFormat::LOOK_NAME_BYTE_LIMIT);
                ensure("notecard name within limit", ALLookFormat::notecardNameForLook(look.metadata.name).size() <= ALLookFormat::LOOK_NAME_BYTE_LIMIT);
                std::string composed_back;
                ALLookFormat::composeNotecardText(look, composed_back);
            }
            // Raw bytes through the name cleaner and the LUT cut.
            const std::string head    = text.substr(0, 300);
            const std::string cleaned = ALLookFormat::cleanMetadataText(head, 63);
            ensure("cleaned within limit", cleaned.size() <= 63);
            // The head of a Look is mostly markup: none of it may survive
            // into a name a notification shows inside <nolink>.
            ensure("cleaned without markup's brackets", cleaned.find_first_of("<>") == std::string::npos);
            ALLookFormat::lookNameFromUntrustedText(head);
            ensure("a notecard's name without them", ALLookFormat::lookNameFromNotecardName(head).find_first_of("<>") == std::string::npos);
            const std::string lut = ALLookFormat::lutFileNameOnly(head);
            ensure("lut has no separator", lut.find_first_of("/\\") == std::string::npos);
            ALLookFormat::numberedLookName(head, 2);
        }
    } // namespace

    template<> template<> void object::test<19>()
    {
        set_test_name("mutated and deeply nested text");
        LLSD small                            = LLSD::emptyMap();
        small["RenderExposure"]               = entry("F32", 0.25);
        small["RenderGlowIterations"]         = entry("S32", 3);
        small["AlchemyExposureMeteringMode"]  = entry("U32", 1);
        small["RenderColorGrade"]             = entry("Boolean", true);
        small["RenderGlowNoise"]              = entry("Boolean", 1.0);
        small["AlchemyRenderTonemapType"]     = entry("S32", "5");
        small["RenderVignetteColor"]          = entry("Color3", triple(0.1, 0.2, 0.3));
        small["RenderColorGradeLift"]         = entry("Vector3", triple(-0.1, 0.0, 0.1));
        small["RenderColorGradeCurveRed"]     = entry("LLSD", curve(5));
        small["RenderColorGradeLUT"]          = entry("String", "warm.cube");
        std::vector<std::string> seeds;
        seeds.push_back(composed(lookNamed("Seed Look \xC3\xA9", small)));
        seeds.push_back(
            "<?xml version=\"1.0\" ?>\n<llsd><map><key>@AlchemyLook</key><map><key>Format</key><integer>1</integer><key>Name</key><string>Hand</string>"
            "<key>Date</key><date>2026-10-06T12:00:00Z</date><key>Author</key><string>A &amp; B &lt;c&gt; &#x41; &#66;</string></map>"
            "<key>RenderA</key><map><key>Type</key><string>F32</string><key>Value</key><real>1.5e3</real></map>"
            "<key>RenderB</key><map><key>Type</key><string>S32</string><key>Value</key><integer>-7</integer></map>"
            "<key>RenderC</key><map><key>Type</key><string>Boolean</string><key>Value</key><boolean>true</boolean></map>"
            "<key>RenderD</key><map><key>Type</key><string>Color3</string><key>Value</key><array><real>0.5</real><integer>1</integer><string>0.25</string><real>1</real></array></map>"
            "<key>RenderE</key><map><key>Type</key><string>LLSD</string><key>Value</key><array><array><real>0</real><real>0</real></array><array><real>1</real><real>1</real></array></array></map>"
            "<key>RenderF</key><map><key>Type</key><string>String</string><key>Value</key><string><![CDATA[a<b]]></string></map>"
            "<key>RenderG</key><map><key>Value</key><binary encoding=\"base64\">AAECAw==</binary></map>"
            "<key>RenderH</key><map><key>Value</key><uuid>6bad5ab6-6c4b-4c2a-8f1e-000000000000</uuid></map>"
            "<key>RenderI</key><map><key>Value</key><uri>http://example.invalid/x</uri></map>"
            "<key>RenderJ</key><map><key>Value</key><undef/></map>"
            "<!-- a comment <with> brackets --></map></llsd>\n");
        static const char* const dict[] = { "<array>", "</array>", "<map>", "</map>", "<key>", "</key>", "<integer>", "</integer>", "<real>", "</real>",
                                            "<string>", "</string>", "<binary encoding=\"base64\">", "<binary>", "</binary>", "<uuid>", "</uuid>", "<date>",
                                            "</date>", "<uri>", "</uri>", "<boolean>", "</boolean>", "<undef/>", "<undef>", "&#x", "&#", "&amp;", "&lt;", "&gt;",
                                            "&quot;", "&apos;", "&", ";", "<![CDATA[", "]]>", "<!--", "-->", "<?xml", "?>", "<llsd>", "</llsd>", "nan", "inf", "-",
                                            "+", ".", "e", "1e999", "0x10", "99999999999999999999", "-2147483649", "4294967296", "\xFF", "\xF4\x80\x80\x80",
                                            "\xC0\x80", "\xED\xA0\x80", "\xE2\x82", "\x00", "<", ">", "/>", "\"", "=", "@AlchemyLook", "Format", "Value",
                                            "Type", "Name", "|", "..", "\\", "/", ":", "\r\n", " ", "\t" };
        const size_t dict_size = sizeof(dict) / sizeof(dict[0]);
        FuzzRng      rng(0x5EEDu);
        size_t       parsed     = 0;
        const size_t iterations = 40000;
        for (size_t iter = 0; iter < iterations; ++iter)
        {
            std::string  text      = seeds[rng.below(seeds.size())];
            const size_t mutations = 1 + rng.below(8);
            for (size_t m = 0; m < mutations && !text.empty(); ++m)
            {
                const size_t at = rng.below(text.size());
                switch (rng.below(8))
                {
                    case 0:
                        text[at] = static_cast<char>(rng.next());
                        break;
                    case 1:
                        text.erase(at, 1 + rng.below(64));
                        break;
                    case 2:
                    {
                        const size_t len = 1 + rng.below(64);
                        text.insert(at, text.substr(at, len));
                        break;
                    }
                    case 3:
                    {
                        const char* tok = dict[rng.below(dict_size)];
                        text.insert(at, tok, tok[0] == '\0' ? 1 : std::strlen(tok));
                        break;
                    }
                    case 4:
                    {
                        std::string junk;
                        for (size_t k = 0, n = 1 + rng.below(16); k < n; ++k)
                        {
                            junk.push_back(static_cast<char>(rng.next()));
                        }
                        text.insert(at, junk);
                        break;
                    }
                    case 5:
                        text.resize(at);
                        break;
                    case 6:
                    {
                        const size_t digits = text.find_first_of("0123456789", at);
                        if (digits != std::string::npos)
                        {
                            text.replace(digits, 1, "99999999999999999999");
                        }
                        break;
                    }
                    case 7:
                    {
                        const size_t      b     = rng.below(text.size());
                        const size_t      len   = 1 + rng.below(32);
                        const std::string chunk = text.substr(at, len);
                        text.replace(at, len, text.substr(b, len));
                        text.insert(std::min(b, text.size()), chunk);
                        break;
                    }
                }
                if (text.size() > ALLookFormat::NOTECARD_BYTE_LIMIT)
                {
                    text.resize(ALLookFormat::NOTECARD_BYTE_LIMIT);
                }
            }
            exerciseReader(text, parsed);
        }
        ensure("some mutants still read as Looks", parsed > 100);

        // Nesting as deep as a notecard allows -- every shape cut to exactly
        // what fits in NOTECARD_BYTE_LIMIT -- and long runs of one thing. The
        // parser is iterative, but LLSD frees a nested value recursively, and
        // the viewer's main thread has the default stack: nine thousand
        // nested maps overflow it, which takes four notecards' worth of text.
        {
            const auto nested = [](const char* open_tag, const char* close_tag, bool close) {
                const std::string head = "<llsd>";
                const std::string tail = "</llsd>";
                const size_t      per  = std::strlen(open_tag) + (close ? std::strlen(close_tag) : 0);
                const size_t      room = ALLookFormat::NOTECARD_BYTE_LIMIT - head.size() - tail.size();
                const size_t      depth = room / per;
                std::string       text = head;
                for (size_t d = 0; d < depth; ++d)
                {
                    text += open_tag;
                }
                if (close)
                {
                    for (size_t d = 0; d < depth; ++d)
                    {
                        text += close_tag;
                    }
                }
                text += tail;
                ensure("fits a notecard", text.size() <= ALLookFormat::NOTECARD_BYTE_LIMIT);
                return std::make_pair(text, depth);
            };
            struct Shape
            {
                const char* name;
                const char* open_tag;
                const char* close_tag;
                bool        close;
            };
            const Shape shapes[] = {
                { "unclosed arrays", "<array>", "</array>", false },
                { "closed arrays", "<array>", "</array>", true },
                { "unclosed maps with empty keys", "<map><key/>", "</map>", false },
                { "unclosed maps with keys", "<map><key></key>", "</map>", false },
                { "closed maps with keys", "<map><key>k</key>", "</map>", true },
                { "unclosed maps", "<map>", "</map>", false },
                { "unclosed llsd", "<llsd>", "</llsd>", false },
                { "unclosed unknown elements", "<x>", "</x>", false },
                { "unclosed arrays of maps", "<array><map><key/>", "</map></array>", false },
            };
            for (const Shape& shape : shapes)
            {
                const auto made = nested(shape.open_tag, shape.close_tag, shape.close);
                ensure(std::string("nests: ") + shape.name, made.second > 2000);
                exerciseReader(made.first, parsed);
            }
        }
        exerciseReader("<llsd><string>" + std::string(65000, 'A') + "</string></llsd>", parsed);
        exerciseReader("<llsd><string>" + std::string(65000, '&') + "</string></llsd>", parsed);
        {
            std::string ents = "<llsd><string>";
            for (int k = 0; k < 8000; ++k)
            {
                ents += "&#x41;";
            }
            ents += "</string></llsd>";
            exerciseReader(ents, parsed);
        }
        {
            std::string ents = "<llsd><string>";
            for (int k = 0; k < 6000; ++k)
            {
                ents += "&#99999999999;";
            }
            ents += "</string></llsd>";
            exerciseReader(ents, parsed);
        }
        {
            std::string ents = "<llsd><string>";
            for (int k = 0; k < 6000; ++k)
            {
                ents += "&#x110000;";
            }
            ents += "</string></llsd>";
            exerciseReader(ents, parsed);
        }
        exerciseReader("<llsd><binary encoding=\"base64\">" + std::string(65000, 'A') + "</binary></llsd>", parsed);
        exerciseReader("<llsd><binary encoding=\"base64\">!!!!</binary></llsd>", parsed);
        exerciseReader("<llsd><integer>" + std::string(60000, '9') + "</integer></llsd>", parsed);
        exerciseReader("<llsd><real>" + std::string(60000, '9') + "</real></llsd>", parsed);
        exerciseReader("<llsd><map><key>" + std::string(65000, 'k') + "</key><integer>1</integer></map></llsd>", parsed);
        exerciseReader(std::string(65000, '<'), parsed);
        exerciseReader("<llsd><map><key>@AlchemyLook</key><map><key>Format</key><integer>1</integer><key>Name</key><string>" + std::string(65000, '\xE3') +
                           "</string></map></map></llsd>",
                       parsed);
    }
} // namespace tut
