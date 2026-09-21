/**
 * @file alxuifindings.h
 * @brief Every finding the rules have made, kept and asked questions of.
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

#include "alfindings.h"
#include "alxuilint.h"
#include "alxuiselection.h"

#include <string>
#include <string_view>

// What XUI Studio's lint found, in the store every studio's findings live
// in: ALFindings over the lint's Finding, read through these traits.
struct ALXUIFindingTraits
{
    static ALFindingLevel level(const ALXUILint::Finding& finding)
    {
        switch (finding.severity)
        {
            case ALXUILint::Severity::Error:   return ALFindingLevel::Error;
            case ALXUILint::Severity::Warning: return ALFindingLevel::Warning;
            case ALXUILint::Severity::Note:    return ALFindingLevel::Note;
        }
        return ALFindingLevel::Note;
    }
    static std::string rule(const ALXUILint::Finding& finding) { return ALXUILint::ruleName(finding.rule); }
    static bool        fixable(const ALXUILint::Finding& finding) { return finding.fix.did != ALXUILint::Fix::Do::Nothing; }
    static bool        mentions(const ALXUILint::Finding& finding, std::string_view text)
    {
        return ALStringMatch::containsNoCase(finding.message, text) || ALStringMatch::containsNoCase(finding.what, text) ||
               ALStringMatch::containsNoCase(ALXUISelection::toString(finding.path), text);
    }
};

class ALXUIFindings : public ALFindings<ALXUILint::Finding, ALXUIFindingTraits>
{
public:
    using ALFindings::count;
    // As the lint says a level.
    S32 count(ALXUILint::Severity severity) const
    {
        switch (severity)
        {
            case ALXUILint::Severity::Error:   return count(Level::Error);
            case ALXUILint::Severity::Warning: return count(Level::Warning);
            case ALXUILint::Severity::Note:    return count(Level::Note);
        }
        return 0;
    }
};
