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

#include "../test/lltut.h"

#include "../llchatautocompletemodel.h"

namespace tut
{
struct chat_autocomplete_model_data
{
    using Action = LLChatAutocompleteModel::CommitAction;
    using Candidate = LLChatAutocompleteModel::Candidate;
};

typedef test_group<chat_autocomplete_model_data> chat_autocomplete_model_group;
typedef chat_autocomplete_model_group::object chat_autocomplete_model_object;
chat_autocomplete_model_group chat_autocomplete_model_tests("LLChatAutocompleteModel");

template<> template<>
void chat_autocomplete_model_object::test<1>()
{
    Candidate gesture{ "/internet", "/internet", "internet yamero full ver.", false };

    ensure(
        "return submits a fully prefilled gesture",
        LLChatAutocompleteModel::getCommitAction(true, "/internet", gesture) == Action::SUBMIT);
    ensure(
        "gesture comparison ignores typed capitalization",
        LLChatAutocompleteModel::getCommitAction(true, "/INTERNET", gesture) == Action::SUBMIT);
    ensure(
        "return completes a partial gesture",
        LLChatAutocompleteModel::getCommitAction(true, "/inter", gesture) == Action::COMPLETE);
}

template<> template<>
void chat_autocomplete_model_object::test<2>()
{
    Candidate command{ "/plat", "/plat", "Rez a platform", true };
    Candidate immediate_command{ "/home", "/home", "Teleport home", false };

    ensure(
        "return leaves room for supported command arguments",
        LLChatAutocompleteModel::getCommitAction(true, "/plat", command) == Action::COMPLETE);
    ensure(
        "return submits a complete command without arguments",
        LLChatAutocompleteModel::getCommitAction(true, "/home", immediate_command) == Action::SUBMIT);
}

template<> template<>
void chat_autocomplete_model_object::test<3>()
{
    Candidate gesture{ "/internet", "/internet", "internet yamero full ver.", false };
    Candidate command{ "/home", "/home", "Teleport home", false };

    ensure(
        "tab always completes a gesture",
        LLChatAutocompleteModel::getCommitAction(false, "/internet", gesture) == Action::COMPLETE);
    ensure(
        "tab always completes a command",
        LLChatAutocompleteModel::getCommitAction(false, "/home", command) == Action::COMPLETE);
}
}
