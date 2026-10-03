/**
 * @file alscriptstudiosnippetnotecard.cpp
 * @brief The snippet notecard a scripter follows: its snippets offered beside their own, as it stands.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptstudiosnippetnotecard.h"

#include "alfollowednotecard.h"
#include "alscriptsnippets.h"
#include "lltrans.h"

ALScriptStudioSnippetNotecard::~ALScriptStudioSnippetNotecard() = default;

ALScriptStudioSnippetNotecard::ALScriptStudioSnippetNotecard()
    : mNotecard(std::make_unique<ALFollowedNotecard>(SETTING, "script_studio_snippet_notecard.xml", "SnippetsFollowedNotNotecard"))
{
    mNotecardChanged = mNotecard->onChanged([this](bool) {
        take();
        mChanged();
    });
    take();
}

LLUUID ALScriptStudioSnippetNotecard::notecard() const
{
    return mNotecard->item();
}

std::string ALScriptStudioSnippetNotecard::notecardName() const
{
    return mNotecard->name();
}

void ALScriptStudioSnippetNotecard::useNotecard(const LLUUID& item)
{
    mNotecard->use(item);
}

void ALScriptStudioSnippetNotecard::check()
{
    if (mSinceCheck.getElapsedTimeF32() < 1.f)
    {
        return;
    }
    mSinceCheck.reset();
    mNotecard->check();
}

void ALScriptStudioSnippetNotecard::take()
{
    // Text that is not snippets gives none, and says so: what is offered
    // is what the notecard holds.
    mError = mNotecard->error();
    if (!ALScriptSnippets::follow(mNotecard->text()))
    {
        ALScriptSnippets::follow(std::string());
        if (mError.empty())
        {
            mError = LLTrans::getString("SnippetsFollowedNotSnippets");
        }
    }
}
