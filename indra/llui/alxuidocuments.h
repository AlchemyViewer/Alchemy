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

    // --- one action, however many files ---------------------------------------
    // A step in a document is a file put back the way it was. That is not
    // what a person means by undo: repairing the translations of a file
    // writes a base and every language beside it, and lining up a selection
    // writes an attribute on each of several elements. Each of those is one
    // thing done and several steps taken, and undoing it a step at a time
    // undoes something nobody did.
    //
    // So the history lives here, as actions: each names the documents it
    // touched and how many steps it took in each. An edit that did not say
    // it was part of something larger is an action of its own, which is what
    // a single edit looks like to a person anyway.
    class Action
    {
    public:
        explicit Action(ALXUIDocuments& documents);
        ~Action();
        Action(const Action&) = delete;
        Action& operator=(const Action&) = delete;

        // Closed here rather than at the end of its scope, for a caller
        // that lists the history before the scope ends: nothing settles
        // while an action is open, and a list filled then is one behind.
        void close();
    private:
        ALXUIDocuments& mDocuments;
        bool            mOpen = true;
    };

    bool canUndo() const { return !mDone.empty(); }
    bool canRedo() const { return !mUndone.empty(); }
    bool undo();
    bool redo();

    // One action, as something listing them reads it.
    struct Entry
    {
        // What its last step did, which for an action of one step is what
        // the action did. A caller says it in words.
        ALXUIEdit::Change   change;
        // The document it touched, where it touched exactly one.
        std::string         document;
        S32                 steps = 0;
        S32                 documents = 0;
    };

    // Everything done, oldest first, and then everything put back -- one
    // list, with inForce() saying where the present is in it. Putting a step
    // back does not make it never have happened, and a stack a person can
    // see is the whole reason to keep them in one place.
    std::vector<Entry> history() const;
    size_t inForce() const { return mDone.size(); }

    // What the last undo or redo put back, where it was one step of one
    // document. An action over more than that says only that it was more
    // than that, since there is no one field for a caller to write.
    const ALXUIEdit::Change& lastChange() const { return mLastChange; }
    const ALXUIEdit::path_t& lastPath() const { return mLastPath; }
    const std::string& lastDocument() const { return mLastDocument; }

    // Steps taken since this last looked, made into actions of their own.
    // Called where a caller has finished doing something, and quiet while
    // an Action is open, which is what makes an Action one thing.
    void settle();

    // How many have work in them the disk has not heard about.
    S32 dirtyCount() const;

    // The disk changed under what is held. A document with no edits in
    // hand is read again, since the disk is the newer copy; one with
    // edits is left as it is, and the caller says so. A document read
    // again has no history, and an action that named it cannot be put
    // back: the history goes where any of them had one. Answers how many
    // were read again.
    S32 rereadClean();

    // Every one of those written. Answers how many, and stops at the first
    // that refuses -- a caller told "three of five" with no word about the
    // fourth has been told nothing it can act on.
    S32 saveAll();

    const std::string& error() const { return mError; }

private:
    // One thing done: which documents it took steps in, and how many in
    // each, in the order they were taken.
    struct Taken
    {
        std::vector<std::pair<std::string, S32> > steps;
        // What the last of them did, read as the action closed: a caller
        // listing actions has to say what each one was, and the steps
        // themselves are in the documents rather than here.
        ALXUIEdit::Change what;
    };

    // How many steps each document had taken when this last looked.
    void remember();

    boost::unordered_flat_map<std::string, size_t, ll::string_hash, std::equal_to<> > mSeen;
    std::vector<Taken>          mDone;
    std::vector<Taken>          mUndone;
    S32                         mOpenActions = 0;
    ALXUIEdit::Change           mLastChange;
    ALXUIEdit::path_t           mLastPath;
    std::string                 mLastDocument;

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
