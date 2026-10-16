/**
 * @file alscriptstudiosnippetnotecard.h
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

#pragma once

#include "llsingleton.h"
#include "lltimer.h"
#include "lluuid.h"

#include <boost/signals2.hpp>

#include <memory>
#include <string>

class ALFollowedNotecard;

// The snippet notecard a scripter follows, dropped on the box for it in
// the studio's preferences: its snippets offered beside the viewer's and
// their own (ALScriptSnippets::follow) as the notecard stands, whoever
// changes it -- a team's notecard, kept up to date by one of them. Followed
// by ALFollowedNotecard, which keeps its text on disk with the asset it
// was read from.
class ALScriptStudioSnippetNotecard final : public LLSingleton<ALScriptStudioSnippetNotecard>
{
    LLSINGLETON(ALScriptStudioSnippetNotecard);
    ~ALScriptStudioSnippetNotecard() override;

public:
    // The per-account setting that holds the notecard's item.
    static constexpr const char* SETTING = "ALScriptStudioSnippetsNotecard";

    // The notecard followed, null where none is; its name, as last seen;
    // and why its snippets could not be read the last time they were
    // tried, empty where they were.
    LLUUID             notecard() const;
    std::string        notecardName() const;
    const std::string& error() const { return mError; }
    // The notecard dropped on the box, or null for none.
    void               useNotecard(const LLUUID& item);

    // Said whenever its snippets, or which notecard it is, change.
    typedef boost::signals2::signal<void()> changed_signal_t;
    boost::signals2::connection onChanged(const changed_signal_t::slot_type& slot) { return mChanged.connect(slot); }

    // The notecard fetched where its item has another asset now. At most
    // once a second, for a caller each frame.
    void check();

private:
    void take();

    std::string                         mError;
    LLTimer                             mSinceCheck;
    std::unique_ptr<ALFollowedNotecard> mNotecard;
    boost::signals2::scoped_connection  mNotecardChanged;
    changed_signal_t                    mChanged;
};
