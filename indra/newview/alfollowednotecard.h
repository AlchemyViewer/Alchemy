/**
 * @file alfollowednotecard.h
 * @brief A notecard in inventory followed for its text, kept on disk while its asset is unchanged.
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

#include "lluuid.h"

#include <boost/signals2.hpp>

#include <memory>
#include <string>

// A notecard in inventory whose text something follows -- the vimrc,
// snippets -- by its item, which a per-account setting holds, since the
// notecard is the account's.
//
// Its text is kept on disk with the asset it was read from. An asset never
// changes: an edit to the notecard makes a new one and gives the item its
// id, so while the item's asset is the one kept the copy is the notecard as
// it stands, and it is read from disk rather than fetched again -- at once,
// before inventory has so much as loaded. An item that has moved on to
// another asset is fetched, the copy standing in until the new text comes.
class ALFollowedNotecard
{
public:
    // The per-account setting holding the item; the file, in the account's
    // folder, the copy is kept in; and the string said where the item is
    // not a notecard that reads, by its name in strings.xml.
    ALFollowedNotecard(std::string setting, std::string cache_file, std::string not_notecard);
    ~ALFollowedNotecard();

    // The notecard followed, null where none is; its name, as last seen.
    LLUUID      item() const;
    std::string name() const;
    // Its text as last read, and why it could not be read the last time it
    // was tried, empty where it was.
    const std::string& text() const { return mText; }
    const std::string& error() const { return mError; }
    // Another notecard followed, or none with null.
    void use(const LLUUID& item);

    // Fetched again where the item has another asset now -- saved since,
    // here or elsewhere -- or has come into inventory with one.
    void check();

    // Said whenever the text or the error changes; `moved` where it is
    // another notecard, or none, whatever its text.
    typedef boost::signals2::signal<void(bool moved)> changed_signal_t;
    boost::signals2::connection onChanged(const changed_signal_t::slot_type& slot) { return mChanged.connect(slot); }

private:
    void        refresh();
    void        fetch(const LLUUID& item);
    std::string cachePath() const;
    void        loadCache();
    void        saveCache();
    void        take(const std::string& text, const std::string& error);

    const std::string mSetting;
    const std::string mCacheFile;
    const std::string mNotNotecard;
    std::string       mText;
    std::string       mError;
    // The copy kept on disk: whose, from which asset, its name and its
    // text.
    bool        mCacheLoaded = false;
    LLUUID      mCacheItem;
    LLUUID      mCacheAsset;
    std::string mCacheName;
    std::string mCacheText;
    // The asset a fetch is on its way for, so that another is not asked.
    LLUUID mFetching;
    // While another notecard is taken up: said once, as moved.
    bool                               mMoving = false;
    boost::signals2::scoped_connection mSettingChanged;
    changed_signal_t                   mChanged;
    // Whether this is still here, for what a fetch calls back.
    std::shared_ptr<bool> mAlive = std::make_shared<bool>(true);
};
