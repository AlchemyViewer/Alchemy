/**
 * @file alxuidocuments.cpp
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

#include "linden_common.h"

#include "alxuidocuments.h"

#include <algorithm>

ALXUIDocuments::ALXUIDocuments() = default;
ALXUIDocuments::~ALXUIDocuments() = default;

ALXUIEdit* ALXUIDocuments::open(const std::string& path)
{
    mError.clear();
    if (path.empty())
    {
        return nullptr;
    }
    if (ALXUIEdit* already = find(path))
    {
        makeActive(path);
        return already;
    }

    auto held = std::make_unique<ALXUIEdit>();
    if (!held->loadFile(path))
    {
        mError = held->error();
        return nullptr;
    }
    ALXUIEdit* opened = held.get();
    mOpen.emplace(path, std::move(held));
    mPaths.push_back(path);
    mSeen.emplace(path, 0u);
    makeActive(path);
    return opened;
}

ALXUIEdit* ALXUIDocuments::find(std::string_view path)
{
    const auto it = mOpen.find(path);
    return it == mOpen.end() ? nullptr : it->second.get();
}

const ALXUIEdit* ALXUIDocuments::find(std::string_view path) const
{
    const auto it = mOpen.find(path);
    return it == mOpen.end() ? nullptr : it->second.get();
}

const std::string* ALXUIDocuments::textFor(std::string_view path) const
{
    const ALXUIEdit* held = find(path);
    return held ? &held->text() : nullptr;
}

void ALXUIDocuments::makeActive(std::string_view path)
{
    if (ALXUIEdit* held = find(path))
    {
        mActivePath.assign(path);
        mActivePtr = held;
    }
}

bool ALXUIDocuments::close(std::string_view path)
{
    const auto it = mOpen.find(path);
    if (it == mOpen.end())
    {
        return false;
    }
    const bool was_active = mActivePtr == it->second.get();
    mOpen.erase(it);
    mPaths.erase(std::remove(mPaths.begin(), mPaths.end(), path), mPaths.end());
    mSeen.erase(std::string(path));
    // An action naming a document that has gone cannot be put back, and an
    // action half put back is worse than none: the history goes with it.
    mDone.clear();
    mUndone.clear();
    if (was_active)
    {
        // Whatever is left, so that a tool with something open is never
        // holding nothing: the one opened before this is the one a person
        // was looking at before this.
        mActivePath.clear();
        mActivePtr = nullptr;
        if (!mPaths.empty())
        {
            makeActive(mPaths.back());
        }
    }
    return true;
}

void ALXUIDocuments::closeAll()
{
    mOpen.clear();
    mPaths.clear();
    mSeen.clear();
    mDone.clear();
    mUndone.clear();
    mActivePath.clear();
    mActivePtr = nullptr;
}

ALXUIDocuments::Action::Action(ALXUIDocuments& documents)
:   mDocuments(documents)
{
    // Whatever was done before this is done: what happens from here is the
    // one thing this Action stands for.
    mDocuments.settle();
    ++mDocuments.mOpenActions;
}

ALXUIDocuments::Action::~Action()
{
    if (--mDocuments.mOpenActions == 0)
    {
        mDocuments.settle();
    }
}

void ALXUIDocuments::remember()
{
    mSeen.clear();
    for (const std::string& path : mPaths)
    {
        const ALXUIEdit* held = find(path);
        mSeen.emplace(path, held ? held->undoDepth() : 0u);
    }
}

void ALXUIDocuments::settle()
{
    if (mOpenActions > 0)
    {
        return;     // still inside something, so it is not finished
    }

    Taken taken;
    for (const std::string& path : mPaths)
    {
        const ALXUIEdit* held = find(path);
        if (!held)
        {
            continue;
        }
        const auto seen = mSeen.find(path);
        const size_t before = seen == mSeen.end() ? 0u : seen->second;
        if (held->undoDepth() > before)
        {
            taken.steps.emplace_back(path, (S32)(held->undoDepth() - before));
            // The step on top is the last one this action took, which is
            // what an action of one step did and the nearest thing to a
            // description of one that took several.
            if (const ALXUIEdit::Change* what = held->nextUndo())
            {
                taken.what = *what;
            }
        }
    }
    remember();
    if (taken.steps.empty())
    {
        return;
    }

    // Everything taken since this last looked is one thing, because looking
    // is what says a thing has finished: a caller settles after each edit,
    // and an Action is a caller saying not to settle in the middle of one.
    mDone.push_back(std::move(taken));
    mUndone.clear();
}

bool ALXUIDocuments::undo()
{
    settle();
    if (mDone.empty())
    {
        return false;
    }
    const Taken taken = mDone.back();
    mDone.pop_back();

    mLastChange = ALXUIEdit::Change();
    mLastPath.clear();
    mLastDocument.clear();
    // Backwards, since the last step taken is the first put back.
    for (auto it = taken.steps.rbegin(); it != taken.steps.rend(); ++it)
    {
        ALXUIEdit* held = find(it->first);
        for (S32 i = 0; held && i < it->second; ++i)
        {
            held->undo();
        }
        if (held && taken.steps.size() == 1 && it->second == 1)
        {
            mLastChange = held->lastChange();
            mLastPath = held->lastPath();
            mLastDocument = it->first;
        }
    }
    mUndone.push_back(taken);
    remember();
    return true;
}

bool ALXUIDocuments::redo()
{
    settle();
    if (mUndone.empty())
    {
        return false;
    }
    const Taken taken = mUndone.back();
    mUndone.pop_back();

    mLastChange = ALXUIEdit::Change();
    mLastPath.clear();
    mLastDocument.clear();
    for (const auto& [path, count] : taken.steps)
    {
        ALXUIEdit* held = find(path);
        for (S32 i = 0; held && i < count; ++i)
        {
            held->redo();
        }
        if (held && taken.steps.size() == 1 && count == 1)
        {
            mLastChange = held->lastChange();
            mLastPath = held->lastPath();
            mLastDocument = path;
        }
    }
    mDone.push_back(taken);
    remember();
    return true;
}

std::vector<ALXUIDocuments::Entry> ALXUIDocuments::history() const
{
    std::vector<Entry> all;
    all.reserve(mDone.size() + mUndone.size());

    const auto describe = [&all](const Taken& taken)
    {
        Entry entry;
        entry.change = taken.what;
        entry.documents = (S32)taken.steps.size();
        for (const auto& [path, count] : taken.steps)
        {
            entry.steps += count;
        }
        if (taken.steps.size() == 1)
        {
            entry.document = taken.steps.front().first;
        }
        all.push_back(std::move(entry));
    };

    for (const Taken& taken : mDone)
    {
        describe(taken);
    }
    // The most recently put back is the one nearest the present, so it comes
    // first of those: read forwards, the list is one timeline.
    for (auto it = mUndone.rbegin(); it != mUndone.rend(); ++it)
    {
        describe(*it);
    }
    return all;
}

S32 ALXUIDocuments::dirtyCount() const
{
    S32 count = 0;
    for (const auto& [path, held] : mOpen)
    {
        count += held->dirty() ? 1 : 0;
    }
    return count;
}

S32 ALXUIDocuments::rereadClean()
{
    S32 read = 0;
    bool had_history = false;
    for (const std::string& path : mPaths)
    {
        ALXUIEdit* held = find(path);
        if (!held || held->dirty())
        {
            continue;
        }
        had_history = had_history || held->undoDepth() > 0 || held->canRedo();
        if (held->loadFile(path))
        {
            ++read;
        }
    }
    if (had_history)
    {
        mDone.clear();
        mUndone.clear();
    }
    remember();
    return read;
}

S32 ALXUIDocuments::saveAll()
{
    mError.clear();
    S32 written = 0;
    // In the order they were opened, so that the one named in an error is
    // the one a person would expect to hear about first.
    for (const std::string& path : mPaths)
    {
        ALXUIEdit* held = find(path);
        if (!held || !held->dirty())
        {
            continue;
        }
        if (!held->save())
        {
            mError = held->error();
            break;
        }
        ++written;
    }
    return written;
}
