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

#include <algorithm>
#include <cmath>
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
        ensure("opens with the declaration or the note", text.rfind("<?xml", 0) == 0 || text.rfind("\n<!--", 0) == 0);
        ensure("the note follows the declaration", text.find("<!-- Alchemy Lightbox Look") != std::string::npos);

        ALLookFormat::SharedLook back;
        ensure("as written", ALLookFormat::parseNotecardText(text, back));
        ensure("after a byte-order mark", ALLookFormat::parseNotecardText("\xEF\xBB\xBF" + text, back));
        ensure("after blank lines", ALLookFormat::parseNotecardText("\n\n\n" + text, back));
        ensure("after a line of prose", ALLookFormat::parseNotecardText("my favourite look\n" + text, back));

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
        LLSD five = four;
        five.append(1.0);
        ensure("five refused", !accepts("Color3", five));
        LLSD words = LLSD::emptyArray();
        words.append("r");
        words.append("g");
        words.append("b");
        ensure("words refused", !accepts("Color3", words));
        ensure("NaN refused", !accepts("Vector3", triple(0.0, std::numeric_limits<F64>::quiet_NaN(), 0.0)));
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

        LLSD out;
        ensure("the setting goes through the same cut", ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", "..\\..\\x.cube", out));
        ensure_equals("and comes out bare", out.asString(), "x.cube");
        ensure("a control character refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", std::string("warm\n.cube"), out));
        ensure("too long refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", std::string(300, 'x') + ".cube", out));
        ensure("a number refused", !ALLookFormat::sanitizeLookValue("RenderColorGradeLUT", "String", 5, out));
        ensure("another string setting is not cut", ALLookFormat::sanitizeLookValue("RenderSomeName", "String", "a/b", out));
        ensure_equals("and keeps its slash", out.asString(), "a/b");
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
        ensure_equals("trimmed", ALLookFormat::lookNameFromUntrustedText("  Soft Film  "), "Soft Film");
        ensure_equals("accents stay", ALLookFormat::lookNameFromUntrustedText("Caf\xC3\xA9"), "Caf\xC3\xA9");
        ensure_equals("spaces alone are nothing", ALLookFormat::lookNameFromUntrustedText("   "), "");
        ensure_equals("dots alone are nothing", ALLookFormat::lookNameFromUntrustedText(". . ."), "");
        ensure_equals("nothing is nothing", ALLookFormat::lookNameFromUntrustedText(""), "");

        const std::string notecard = ALLookFormat::notecardNameForLook(std::string(70, 'L'));
        ensure("the notecard's name fits an item", notecard.size() <= ALLookFormat::LOOK_NAME_BYTE_LIMIT);
        ensure("and opens with the prefix", notecard.rfind(std::string(ALLookFormat::NOTECARD_NAME_PREFIX), 0) == 0);
        ensure_equals("a short one is whole", ALLookFormat::notecardNameForLook("Soft Film"), "Lightbox Look: Soft Film");
        ensure_equals("the name comes back out", ALLookFormat::lookNameFromNotecardName("Lightbox Look: Soft Film"), "Soft Film");
        ensure_equals("a notecard named otherwise is taken whole", ALLookFormat::lookNameFromNotecardName("My Note"), "My Note");
        ensure_equals("a prefix alone is nothing", ALLookFormat::lookNameFromNotecardName("Lightbox Look: "), "");
    }

    // The note is static text: a name that would end a comment cannot.
    template<> template<> void object::test<16>()
    {
        set_test_name("a name that would end the note");
        LLSD settings              = LLSD::emptyMap();
        settings["RenderExposure"] = entry("F32", 0.5);
        const std::string name     = "Soft --> Film -- take 2";
        const std::string text     = composed(lookNamed(name, settings));
        const size_t      note     = text.find("<!--");
        const size_t      note_end = text.find("-->", note);
        ensure("the note ends where it was written to", text.substr(note, note_end - note).find(name) == std::string::npos);
        ALLookFormat::SharedLook back;
        ensure("and the Look reads", ALLookFormat::parseNotecardText(text, back));
        ensure_equals("with its name", back.metadata.name, name);
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
} // namespace tut
