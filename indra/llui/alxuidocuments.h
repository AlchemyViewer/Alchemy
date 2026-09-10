/**
 * @file alxuidocuments.h
 * @brief The XUI files a tool has open at once, each with its own edits.
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

#include "alxuiedit.h"

#include "llstl.h"

#include <memory>
#include <string>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

// The files a tool has open, keyed by the path they came from. Each carries
// its own text, its own undo stack and its own dirty flag, because each is a
// file: what a person does to one of them is not a step of another.
//
// A tool holding one document at a time has to refuse every move to a second
// one while there is work in hand, and those refusals are what this exists to
// remove. A file that names another in a `filename=`, a base and the language
// overlay that translates it, a skin and the default it varies -- each of
// those is two files a person is working on together, and asking them to save
// before looking at the second is asking them to decide before they have seen
// it.
//
// One of them is **active**: the one an operation with no path of its own
// means. Every other question here takes the path it is about.
class ALXUIDocuments
{
public:
    ALXUIDocuments();
    ~ALXUIDocuments();

    ALXUIDocuments(const ALXUIDocuments&) = delete;
    ALXUIDocuments& operator=(const ALXUIDocuments&) = delete;

    // The document for a path, read from the file the first time it is
    // asked for and held afterwards. Null only where the file could not be
    // read, and then error() says why. Opening one makes it the active one:
    // asking for a document is what working on it looks like.
    ALXUIEdit* open(const std::string& path);

    // The one for a path if it is open, without opening it.
    ALXUIEdit* find(std::string_view path);
    const ALXUIEdit* find(std::string_view path) const;

    // The text a caller merging a file's layers should read in place of the
    // file, where that file is open here: what is being worked on is in
    // memory, and the disk has not heard about it. Null for a path nobody
    // has open, which means read the file.
    const std::string* textFor(std::string_view path) const;

    // The one an operation with no path of its own means. Answered even when
    // nothing is open, with a document of no file, so that a caller reading
    // it does not have to ask twice.
    ALXUIEdit& active() { return mActivePtr ? *mActivePtr : mNone; }
    const ALXUIEdit& active() const { return mActivePtr ? *mActivePtr : mNone; }
    const std::string& activePath() const { return mActivePath; }
    bool hasActive() const { return mActivePtr != nullptr; }
    void makeActive(std::string_view path);

    // Let go of one. Its edits go with it, so a caller asks first.
    bool close(std::string_view path);
    void closeAll();

    size_t count() const { return mOpen.size(); }
    const std::vector<std::string>& paths() const { return mPaths; }

    // How many have work in them the disk has not heard about.
    S32 dirtyCount() const;

    // Every one of those written. Answers how many, and stops at the first
    // that refuses -- a caller told "three of five" with no word about the
    // fourth has been told nothing it can act on.
    S32 saveAll();

    const std::string& error() const { return mError; }

private:
    // Held by pointer because a caller keeps one across the opening of
    // another, and because a document is neither copied nor moved.
    boost::unordered_flat_map<std::string, std::unique_ptr<ALXUIEdit>,
                              ll::string_hash, std::equal_to<> > mOpen;
    // In the order they were opened, for a caller listing them.
    std::vector<std::string>    mPaths;
    std::string                 mActivePath;
    ALXUIEdit*                  mActivePtr = nullptr;
    ALXUIEdit                   mNone;      // what active() answers with
    std::string                 mError;
};
