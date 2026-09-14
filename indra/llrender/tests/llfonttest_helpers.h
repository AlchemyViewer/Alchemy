/**
 * @file llfonttest_helpers.h
 * @brief What the font suites share: where the fonts are, a face key
 *        and a loaded face with the usual defaults, and a test string in
 *        codepoints and bytes at once.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#ifndef LL_LLFONTTEST_HELPERS_H
#define LL_LLFONTTEST_HELPERS_H

#include "linden_common.h"

#include "../alfontface.h"
#include "../llfontfreetype.h"
#include "../llfontregistry.h"

#include "llfile.h"
#include "llstring.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

// The source-tree font directory and the application directory, so the
// tests read the package's faces rather than whatever an install staged.
// CMake passes both as string literals; a test with no fonts skips.
#ifndef LLFONT_TEST_DATA_DIR
#  define LLFONT_TEST_DATA_DIR ""
#endif
#ifndef LLFONT_TEST_APP_DIR
#  define LLFONT_TEST_APP_DIR ""
#endif

namespace ll_test
{
    constexpr const char* kFontDir = LLFONT_TEST_DATA_DIR;
    constexpr const char* kAppDir  = LLFONT_TEST_APP_DIR;

    inline bool fileExists(const std::string& path)
    {
        if (FILE* f = LLFile::fopen(path.c_str(), LLFILE_MODE("rb")))
        {
            std::fclose(f);
            return true;
        }
        return false;
    }

    // A face key at 96 DPI with default hinting and no flags: a typical
    // UI-text size, and the same key LLFontFreetype::loadFace builds for the
    // same file and size, so a face loaded either way is the one face.
    inline ALFontFaceKey makeKey(const std::string& filename, F32 point_size = 14.f)
    {
        return ALFontFaceKey{
            filename, /*face_index=*/0, point_size,
            /*vert_dpi=*/96.f, /*horz_dpi=*/96.f,
            EFontHinting::DEFAULT, /*flags=*/0
        };
    }

    // An LLFontFreetype over `filename`, a fallback by default: the head
    // path in LLFontFreetype::loadFace pre-warms the notdef glyph through
    // the rasterizer, the atlas and gGL.bind, which is fatal without a GL
    // context. Shaping and metrics read only cmap, GSUB and advances, which
    // the flag does not touch.
    inline LLPointer<LLFontFreetype> loadFt(const std::string& filename,
                                            bool is_fallback = true,
                                            F32 point_size = 14.f,
                                            S32 weight = -1,
                                            EFontHinting hinting = EFontHinting::DEFAULT)
    {
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        ALFontVarAxes va;
        if (weight >= 0)
        {
            va.wght = static_cast<F32>(weight);
            va.wght_set = true;
        }
        if (!ft->loadFace(filename, point_size, /*vert_dpi=*/96.f, /*horz_dpi=*/96.f,
                          is_fallback, /*face_n=*/0, hinting, /*flags=*/0, va))
        {
            return nullptr;
        }
        return ft;
    }

    // A real head face, for a test with a GL context to rasterize into.
    inline LLPointer<LLFontFreetype> loadFtHead(const std::string& filename)
    {
        return loadFt(filename, /*is_fallback=*/false);
    }

    // A codepoint sequence from a parameter pack.
    template <typename... Cps>
    std::u32string wstr(Cps... cps)
    {
        const char32_t arr[] = { static_cast<char32_t>(cps)... };
        return std::u32string(arr, sizeof...(Cps));
    }

    // llstring holds no UTF-32 any more, so the encode lives here.
    inline std::string encode(std::u32string_view u32)
    {
        std::string out;
        for (char32_t cp : u32)
        {
            utf8str_append_cp(out, (llwchar)cp);
        }
        return out;
    }

    // A test string in both representations. The sequences under test are
    // specified in codepoints, which is how they are written; shaping takes
    // and reports bytes. `at` turns a codepoint index into the byte offset
    // an expectation has to compare against, so a test can go on saying
    // "the cluster starts at the third codepoint" and still check the value
    // the shaper actually produces.
    struct Text
    {
        std::u32string      wide;
        std::string         utf8;
        std::vector<size_t> offsets;   // one per codepoint, plus the end

        explicit Text(std::u32string ws) : wide(std::move(ws))
        {
            offsets.reserve(wide.size() + 1);
            for (size_t k = 0; k <= wide.size(); ++k)
                offsets.push_back(encode(wide.substr(0, k)).size());
            utf8 = encode(wide);
        }

        size_t at(size_t cp) const { return offsets[llmin(cp, offsets.size() - 1)]; }
        size_t size() const { return utf8.size(); }
        size_t codepoints() const { return wide.size(); }
        operator std::string_view() const { return utf8; }
    };

    template <typename... Cps>
    Text text(Cps... cps)
    {
        return Text(wstr(cps...));
    }
}

#endif // LL_LLFONTTEST_HELPERS_H
