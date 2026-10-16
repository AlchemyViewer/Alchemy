/**
 * @file alchatautocomplete.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alchatautocomplete.h"

#include "alchatcommand.h"
#include "llchatentry.h"
#include "llgesturemgr.h"
#include "llmultigesture.h"
#include "llviewercontrol.h"

#include <map>
#include <utility>

namespace
{
constexpr size_t MAX_AUTOCOMPLETE_ROWS = 50;

std::string lowercase(std::string value)
{
    LLStringUtil::toLower(value);
    return value;
}

void addCandidate(
    std::map<std::string, LLGestureAutocompleteHelper::Row>& candidates,
    const std::string& prefix,
    const std::string& trigger,
    const std::string& name,
    bool accepts_arguments)
{
    if (trigger.empty() || trigger[0] != '/')
    {
        return;
    }

    const std::string lower_trigger = lowercase(trigger);
    if (lower_trigger.compare(0, prefix.size(), prefix) != 0)
    {
        return;
    }

    candidates.try_emplace(
        lower_trigger,
        LLGestureAutocompleteHelper::Row{ trigger, trigger, name, accepts_arguments });
}

bool buildRows(
    const std::string& prefix,
    std::vector<LLGestureAutocompleteHelper::Row>& rows,
    size_t& total)
{
    rows.clear();
    total = 0;

    if (prefix.size() < 2 || prefix[0] != '/' || prefix.find_first_of(" \t") != std::string::npos)
    {
        return false;
    }

    const std::string lower_prefix = lowercase(prefix);
    std::map<std::string, LLGestureAutocompleteHelper::Row> candidates;

    if (gSavedSettings.getBOOL("ChatAutocompleteGestures"))
    {
        const LLGestureMgr::item_map_t& active = LLGestureMgr::instance().getActiveGestures();
        for (const auto& entry : active)
        {
            LLMultiGesture* gesture = entry.second;
            if (gesture)
            {
                addCandidate(
                    candidates,
                    lower_prefix,
                    gesture->getTrigger(),
                    gesture->mName,
                    false);
            }
        }
    }

    for (const ALChatCommand::AutocompleteCommand& command : ALChatCommand::getAutocompleteCommands())
    {
        addCandidate(
            candidates,
            lower_prefix,
            command.trigger,
            command.description,
            command.accepts_arguments);
    }

    addCandidate(candidates, lower_prefix, "/me", "Pose: message", true);
    addCandidate(candidates, lower_prefix, "/shout", "Shout: message", true);
    addCandidate(candidates, lower_prefix, "/whisper", "Whisper: message", true);

    total = candidates.size();
    for (const auto& candidate : candidates)
    {
        if (rows.size() == MAX_AUTOCOMPLETE_ROWS)
        {
            break;
        }
        rows.push_back(candidate.second);
    }

    return !rows.empty();
}
}

void ALChatAutocomplete::update(
    LLChatEntry* editor,
    const std::string& prefix,
    KEY key,
    CommitCallback commit_cb)
{
    std::vector<LLGestureAutocompleteHelper::Row> rows;
    size_t total = 0;

    if (!buildRows(prefix, rows, total))
    {
        LLGestureAutocompleteHelper::instance().hideHelper(editor);
        return;
    }

    LLGestureAutocompleteHelper::instance().showHelper(
        editor,
        rows,
        total,
        std::move(commit_cb));

    const std::string& match = rows.front().value;
    if (key < KEY_SPECIAL && match.size() > prefix.size())
    {
        editor->setText(prefix + match.substr(prefix.size()));
        editor->selectByCursorPosition(
            static_cast<S32>(prefix.size()),
            static_cast<S32>(match.size()));
    }
}
