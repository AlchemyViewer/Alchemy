/**
 * @file alimportedfonts.h
 * @brief User-imported font files: validation, copy into the per-user font
 *        directory, and the generated imported_fonts.xml overlay the font
 *        registry parses.
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

#ifndef AL_ALIMPORTEDFONTS_H
#define AL_ALIMPORTEDFONTS_H

#include "llsd.h"

#include <string>
#include <vector>

// Manages user-imported font files and the overlay the font registry parses.
class ALImportedFonts
{
public:
    // Detected from FreeType flags at import time.
    enum class Category
    {
        PROPORTIONAL,
        MONOSPACE,
        EMOJI,
    };

    struct ImportResult
    {
        enum class RejectReason
        {
            UNSUPPORTED_TYPE,
            UNSAFE_NAME,
            TOO_LARGE,
            UNREADABLE,
            COPY_FAILED,
            OVERLAY_FAILED,
        };

        struct Failure
        {
            std::string  source;
            RejectReason reason;
        };

        S32                    families_added = 0;
        S32                    files_copied   = 0;
        std::vector<Failure>   failed;

        bool anyCopied() const { return files_copied > 0; }
    };

    // Import files, copying valid ones into the per-user font directory.
    static bool importFiles(const std::vector<std::string>& sources, ImportResult& result);

    // Remove an imported family and its copied files.
    static bool removeFamily(const std::string& family_name);

    static LLSD getImported();

    // Display suffix for the family's category, e.g. " (mono)".
    static std::string categorySuffix(const LLSD& family_entry);

    // Rewrite imported_fonts.xml from the current setting.
    static std::string regenerateOverlay();

    static std::string getOverlayPath();

    // Per-user font directory.
    static std::string getUserFontDir(bool create = false);

    static bool isSupportedFontFile(const std::string& path);

    // Clear font overrides, restoring the built-in defaults.
    static void resetOverrides();

private:
    ALImportedFonts() = delete;
};

#endif // AL_ALIMPORTEDFONTS_H
