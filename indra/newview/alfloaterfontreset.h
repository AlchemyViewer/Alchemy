/**
 * @file alfloaterfontreset.h
 * @brief Confirmation dialog for resetting UI fonts to defaults.
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

#ifndef AL_ALFLOATERFONTRESET_H
#define AL_ALFLOATERFONTRESET_H

#include "llfloater.h"

class ALFloaterFontReset final : public LLFloater
{
public:
    ALFloaterFontReset(const LLSD& key);
    ~ALFloaterFontReset() override = default;

    static void showConfirm();

    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    void onReset();
    void onCancel();
};

#endif // AL_ALFLOATERFONTRESET_H
