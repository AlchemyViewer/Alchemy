/**
 * @file llchatautocompletemodel.cpp
 * @brief Data and keyboard policy shared by chat autocomplete controls.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * $/LicenseInfo$
 */

#include "llchatautocompletemodel.h"

#include <algorithm>
#include <cctype>

namespace LLChatAutocompleteModel
{
CommitAction getCommitAction(
    bool return_key,
    const std::string& editor_text,
    const Candidate& candidate)
{
    const bool is_full_match = editor_text.size() == candidate.value.size()
        && std::equal(
            editor_text.begin(),
            editor_text.end(),
            candidate.value.begin(),
            [](unsigned char left, unsigned char right)
            {
                return std::tolower(left) == std::tolower(right);
            });

    if (return_key && is_full_match && !candidate.accepts_arguments)
    {
        return CommitAction::SUBMIT;
    }

    return CommitAction::COMPLETE;
}
}
