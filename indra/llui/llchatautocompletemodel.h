/**
 * @file llchatautocompletemodel.h
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

#pragma once

#include <string>

namespace LLChatAutocompleteModel
{
struct Candidate
{
    std::string value;
    std::string trigger;
    std::string name;
    bool accepts_arguments = false;
};

enum class CommitAction
{
    COMPLETE,
    SUBMIT
};

CommitAction getCommitAction(
    bool return_key,
    const std::string& editor_text,
    const Candidate& candidate);
}
