/**
 * @file alscriptstudiovimrc.h
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

#pragma once

#include "llsingleton.h"
#include "lltimer.h"
#include "lluuid.h"

#include <boost/signals2.hpp>

#include <string>

// The vimrc Script Studio's vim reads: the file vimrc in the viewer's
// settings folder, or a notecard dropped on the box for it in the
// studio's preferences, which is the vimrc while it is set -- kept in the
// account's settings, since the notecard is the account's.
//
// A notecard's text is kept on disk with the asset it was read from.
// An asset never changes: an edit to the notecard makes a new one and
// gives the item its id, so while the item's asset is the one kept the
// copy is the notecard as it stands, and it is read from disk rather
// than fetched again -- at once, before inventory has so much as loaded,
// so that the mappings are there from the first key. An item that has
// moved on to another asset is fetched, the copy standing in until the
// new text comes.
class ALScriptStudioVimrc final : public LLSingleton<ALScriptStudioVimrc>
{
    LLSINGLETON(ALScriptStudioVimrc);

public:
    // The file that is the vimrc while no notecard is.
    static std::string filePath();
    // The per-account setting that holds the notecard's item.
    static constexpr const char* SETTING = "ALScriptStudioVimrcNotecard";

    // The vimrc's text as it stands: the notecard's, as last read, or the
    // file's; empty where there is none.
    const std::string& text() const { return mText; }
    // The notecard that is the vimrc: its item, null while the file is;
    // its name, as last seen; and why it could not be read the last time
    // it was tried, empty where it was.
    LLUUID             notecard() const;
    std::string        notecardName() const;
    const std::string& error() const { return mError; }
    // The notecard dropped on the box, or null for the file again.
    void               useNotecard(const LLUUID& item);

    // Said whenever the text, or where it comes from, changes.
    typedef boost::signals2::signal<void()> changed_signal_t;
    boost::signals2::connection onChanged(const changed_signal_t::slot_type& slot) { return mChanged.connect(slot); }

    // Looked at again: the file, where it is the vimrc, read afresh where
    // it changed on disk since it was read; the notecard fetched where
    // its item has another asset now -- saved since, here or elsewhere --
    // or has come into inventory with one. At most once a second, for a
    // caller each frame; at once with `now`.
    void check(bool now = false);

private:
    // What the vimrc is now, from wherever the setting says.
    void refresh();
    void readFile();
    void fetch(const LLUUID& item);
    // The notecard's copy on disk, read once and written as it changes.
    static std::string cachePath();
    void               loadCache();
    void               saveCache();
    void               take(const std::string& text, const std::string& error);

    std::string      mText;
    std::string      mError;
    // The file as it was when read: when it was last written, and its
    // size; -1 where there was none.
    S64              mFileTime = -1;
    S64              mFileSize = -1;
    LLTimer          mSinceCheck;
    // The notecard's copy kept on disk: whose, from which asset, its name
    // and its text.
    bool             mCacheLoaded = false;
    LLUUID           mCacheItem;
    LLUUID           mCacheAsset;
    std::string      mCacheName;
    std::string      mCacheText;
    // The asset a fetch is on its way for, so that another is not asked.
    LLUUID           mFetching;
    boost::signals2::scoped_connection mSettingChanged;
    changed_signal_t mChanged;
};
