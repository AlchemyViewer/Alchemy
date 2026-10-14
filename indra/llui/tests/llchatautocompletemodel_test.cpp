/**
 * @file llchatautocompletemodel_test.cpp
 * @brief Unit tests for chat autocomplete keyboard behavior.
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

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>

#include "../llchatautocompletemodel.h"

namespace
{
struct chat_autocomplete_model_data
{
    using Action = LLChatAutocompleteModel::CommitAction;
    using Candidate = LLChatAutocompleteModel::Candidate;
};

TEST_CASE_METHOD(chat_autocomplete_model_data, "LLChatAutocompleteModel: return submits a whole gesture and completes a partial one", "[LLChatAutocompleteModel]")
{
    Candidate gesture{ "/internet", "/internet", "internet yamero full ver.", false };

    UNSCOPED_INFO("return submits a fully prefilled gesture");
    CHECK(LLChatAutocompleteModel::getCommitAction(true, "/internet", gesture) == Action::SUBMIT);
    UNSCOPED_INFO("gesture comparison ignores typed capitalization");
    CHECK(LLChatAutocompleteModel::getCommitAction(true, "/INTERNET", gesture) == Action::SUBMIT);
    UNSCOPED_INFO("return completes a partial gesture");
    CHECK(LLChatAutocompleteModel::getCommitAction(true, "/inter", gesture) == Action::COMPLETE);
}

TEST_CASE_METHOD(chat_autocomplete_model_data, "LLChatAutocompleteModel: return completes a command with arguments and submits one without", "[LLChatAutocompleteModel]")
{
    Candidate command{ "/plat", "/plat", "Rez a platform", true };
    Candidate immediate_command{ "/home", "/home", "Teleport home", false };

    UNSCOPED_INFO("return leaves room for supported command arguments");
    CHECK(LLChatAutocompleteModel::getCommitAction(true, "/plat", command) == Action::COMPLETE);
    UNSCOPED_INFO("return submits a complete command without arguments");
    CHECK(LLChatAutocompleteModel::getCommitAction(true, "/home", immediate_command) == Action::SUBMIT);
}

TEST_CASE_METHOD(chat_autocomplete_model_data, "LLChatAutocompleteModel: tab always completes a gesture or a command", "[LLChatAutocompleteModel]")
{
    Candidate gesture{ "/internet", "/internet", "internet yamero full ver.", false };
    Candidate command{ "/home", "/home", "Teleport home", false };

    UNSCOPED_INFO("tab always completes a gesture");
    CHECK(LLChatAutocompleteModel::getCommitAction(false, "/internet", gesture) == Action::COMPLETE);
    UNSCOPED_INFO("tab always completes a command");
    CHECK(LLChatAutocompleteModel::getCommitAction(false, "/home", command) == Action::COMPLETE);
}
}
