/**
 * @file alscriptstudiovimrc.cpp
 * @brief The vimrc Script Studio's vim reads: a file in the settings folder, or a notecard kept on disk while its asset is unchanged.
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

#include "alscriptstudiovimrc.h"

#include "alfilewrite.h"
#include "alfollowednotecard.h"
#include "alwatchedfile.h"
#include "llfile.h"
#include "lltrans.h"

namespace
{
    // More than any vimrc: a file this size is not read.
    constexpr S64 MOST_VIMRC_BYTES = 256 * 1024;
}

ALScriptStudioVimrc::~ALScriptStudioVimrc() = default;

ALScriptStudioVimrc::ALScriptStudioVimrc()
    : mNotecard(std::make_unique<ALFollowedNotecard>(SETTING, "script_studio_vimrc.xml", "VimrcNotNotecard"))
{
    mNotecardChanged = mNotecard->onChanged([this](bool moved) {
        refresh();
        if (moved)
        {
            // Where it comes from changed, whatever the text.
            mChanged();
        }
    });
    refresh();
}

// static
std::string ALScriptStudioVimrc::filePath()
{
    return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "vimrc");
}

LLUUID ALScriptStudioVimrc::notecard() const
{
    return mNotecard->item();
}

std::string ALScriptStudioVimrc::notecardName() const
{
    return mNotecard->name();
}

void ALScriptStudioVimrc::useNotecard(const LLUUID& item)
{
    mNotecard->use(item);
}

void ALScriptStudioVimrc::check(bool now)
{
    if (!now && mSinceCheck.getElapsedTimeF32() < 1.f)
    {
        return;
    }
    mSinceCheck.reset();
    if (notecard().isNull())
    {
        // Watched, and read as it changes; read now where asked, or where
        // it has yet to be watched.
        if (now || !mWatch)
        {
            readFile();
        }
        return;
    }
    mNotecard->check();
}

void ALScriptStudioVimrc::refresh()
{
    if (notecard().isNull())
    {
        readFile();
        return;
    }
    // Not the vimrc while the notecard is.
    mWatch.reset();
    take(mNotecard->text(), mNotecard->error());
}

void ALScriptStudioVimrc::readFile()
{
    const std::string path = filePath();
    // Watched from what is read here: a change after it is heard, and
    // read in turn.
    if (!mWatch || mWatch->path() != path)
    {
        mWatch = std::make_unique<ALWatchedFile>(path, [this](const std::string&) { readFile(); });
        mWatch->poll(1.f);
    }
    else
    {
        mWatch->seen();
    }
    const ALFileStamp stamp = ALFileStamp::of(path);
    if (stamp.size > static_cast<std::uintmax_t>(MOST_VIMRC_BYTES))
    {
        take(std::string(), LLTrans::getString("VimrcTooLarge"));
        return;
    }
    std::string text;
    if (!ALFileRead::whole(path, text, static_cast<std::uintmax_t>(MOST_VIMRC_BYTES)))
    {
        text.clear();
    }
    take(text, std::string());
}

void ALScriptStudioVimrc::take(const std::string& text, const std::string& error)
{
    if (text == mText && error == mError)
    {
        return;
    }
    mText  = text;
    mError = error;
    mChanged();
}
