/**
 * @file alparamfingerprint.h
 * @brief A stable fingerprint of the parameter block behind every llui widget.
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

#pragma once

#include <iosfwd>
#include <string>

namespace ALParamFingerprint
{
    // Builds the default parameter block for every llui widget type, which
    // reads that widget's template from the skin and back-fills it through
    // the whole inheritance chain, and returns one line per type:
    //
    //     <tag> params=<count> xml=<yes|no> table=<hash> value=<hash>
    //
    // table= hashes the shape of the parameter table (names and their
    // min/max counts); value= hashes the block serialized to LLSD notation.
    // Both are needed: value= alone cannot separate two blocks whose extra
    // parameters carry no default.
    //
    // Reworking the descriptor tables, the name stack or the block layout
    // must leave this output byte-identical. Diff it across a change; do not
    // read it. Keep it out of the same stream as census(), which is meant to
    // move -- otherwise every successful optimization reads as a failed gate.
    void collect(std::ostream& out);

    // What the parameter tables cost once every widget above has been built.
    std::string census();

    // What one instance of each widget's parameter block costs. The tables are
    // per type; this is what every widget built from them carries.
    std::string sizes();

    // What it costs to build a parameter block, for the blocks that code
    // builds by hand on a hot path rather than copying from the widget
    // defaults. Reported per block as default construction against copy
    // construction, because a widget parsed from XUI pays the copy and
    // everything else pays the default.
    std::string bench();
}
