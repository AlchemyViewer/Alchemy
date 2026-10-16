/**
 * @file alchatautocomplete.h
 * @brief Gesture and command completion for nearby chat editors.
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

#include "llgestureautocompletehelper.h"

#include <functional>
#include <string>

class LLChatEntry;

namespace ALChatAutocomplete
{
using CommitAction = LLGestureAutocompleteHelper::CommitAction;
using CommitCallback = std::function<void(const LLGestureAutocompleteHelper::Row&, CommitAction)>;

void update(
    LLChatEntry* editor,
    const std::string& prefix,
    KEY key,
    CommitCallback commit_cb);
}
