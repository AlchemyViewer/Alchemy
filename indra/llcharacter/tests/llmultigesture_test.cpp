/**
 * @file llmultigesture_test.cpp
 * @brief Unit tests for LLMultiGesture's reader, against content it did not write.
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

#include "llmultigesture.h"

#include "lldatapacker.h"

#include "../test/lltut.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
    // A gesture with one of each step in it, written the way the viewer writes
    // one, so the reader is fed its own format.
    std::unique_ptr<LLMultiGesture> sample_gesture()
    {
        std::unique_ptr<LLMultiGesture> gesture = std::make_unique<LLMultiGesture>();
        gesture->mKey = 'G';
        gesture->mMask = 3;
        gesture->mTrigger = "/wave";
        gesture->mReplaceText = "waves";

        LLGestureStepAnimation* anim = new LLGestureStepAnimation();
        anim->mAnimName = "express_wave";
        anim->mAnimAssetID.generate();
        anim->mFlags = 1;
        gesture->mSteps.push_back(anim);

        LLGestureStepSound* sound = new LLGestureStepSound();
        sound->mSoundName = "chime";
        sound->mSoundAssetID.generate();
        gesture->mSteps.push_back(sound);

        LLGestureStepChat* chat = new LLGestureStepChat();
        chat->mChatText = "hello";
        gesture->mSteps.push_back(chat);

        LLGestureStepWait* wait = new LLGestureStepWait();
        wait->mWaitSeconds = 1.5f;
        wait->mFlags = 2;
        gesture->mSteps.push_back(wait);

        return gesture;
    }

    // Writes it out and hands back the bytes, terminator included, the way the
    // gesture manager hands them to the reader.
    std::vector<char> serialize(LLMultiGesture& gesture)
    {
        std::vector<char> buffer(gesture.getMaxSerialSize() + 1, '\0');
        LLDataPackerAsciiBuffer dp(buffer.data(), (S32)buffer.size());
        tut::ensure("the gesture serializes", gesture.serialize(dp));
        buffer.resize(dp.getCurrentSize());
        buffer.back() = '\0';
        return buffer;
    }

    // Writes one step out and hands back the bytes, terminator included.
    std::vector<char> serialize_step(const LLGestureStep& step)
    {
        std::vector<char> buffer(step.getMaxSerialSize() + 1, '\0');
        LLDataPackerAsciiBuffer dp(buffer.data(), (S32)buffer.size());
        tut::ensure("the step serializes", step.serialize(dp));
        buffer.resize(dp.getCurrentSize());
        buffer.back() = '\0';
        return buffer;
    }

    // The same bytes with the last field taken off. Each field is a line, so
    // that is everything up to and including the newline before the last one.
    std::vector<char> without_last_field(const std::vector<char>& bytes)
    {
        const std::string text(bytes.data());
        const size_t last = text.find_last_of('\n');
        tut::ensure("the step has a last field", last != std::string::npos && last > 0);
        const size_t previous = text.find_last_of('\n', last - 1);
        tut::ensure("and one before it", previous != std::string::npos);

        std::vector<char> shortened(bytes.begin(), bytes.begin() + previous + 1);
        shortened.push_back('\0');
        return shortened;
    }

    // Reads a step back from bytes.
    bool step_accepts(LLGestureStep& step, const std::vector<char>& bytes)
    {
        std::vector<char> mutableBytes = bytes;
        LLDataPackerAsciiBuffer dp(mutableBytes.data(), (S32)mutableBytes.size());
        return step.deserialize(dp);
    }
}

namespace tut
{
    struct llmultigesture_data
    {
    };
    typedef test_group<llmultigesture_data> llmultigesture_test;
    typedef llmultigesture_test::object llmultigesture_object;
    tut::llmultigesture_test llmultigesture_testcase("LLMultiGesture");

    template<> template<>
    void llmultigesture_object::test<1>()
    {
        // What it writes it reads back.
        std::unique_ptr<LLMultiGesture> written = sample_gesture();
        std::vector<char> bytes = serialize(*written);

        LLMultiGesture read;
        LLDataPackerAsciiBuffer dp(bytes.data(), (S32)bytes.size());
        ensure("a gesture it wrote is accepted", read.deserialize(dp));

        ensure_equals("key", read.mKey, written->mKey);
        ensure_equals("mask", read.mMask, written->mMask);
        ensure_equals("trigger", read.mTrigger, written->mTrigger);
        ensure_equals("replace text", read.mReplaceText, written->mReplaceText);
        ensure_equals("step count", read.mSteps.size(), written->mSteps.size());

        ensure_equals("the animation step came back",
                      static_cast<LLGestureStepAnimation*>(read.mSteps[0])->mAnimName,
                      std::string("express_wave"));
        ensure_equals("the chat step came back",
                      static_cast<LLGestureStepChat*>(read.mSteps[2])->mChatText,
                      std::string("hello"));
    }

    template<> template<>
    void llmultigesture_object::test<2>()
    {
        // Cut anywhere, and it is refused rather than half read. Every prefix
        // of a real gesture, so the cut lands in each field in turn.
        //
        // Every prefix but one: the format ends each field with a newline, and
        // the last of those is the only byte in the file nothing depends on.
        // Dropping just that one leaves every field still readable, and a
        // gesture that reads is a gesture that loads.
        std::unique_ptr<LLMultiGesture> written = sample_gesture();
        const std::vector<char> bytes = serialize(*written);

        for (S32 length = 1; length < (S32)bytes.size() - 1; ++length)
        {
            std::vector<char> truncated(bytes.begin(), bytes.begin() + length);
            truncated.back() = '\0';

            LLMultiGesture read;
            LLDataPackerAsciiBuffer dp(truncated.data(), (S32)truncated.size());
            ensure("a gesture cut short is refused", !read.deserialize(dp));
        }
    }

    template<> template<>
    void llmultigesture_object::test<3>()
    {
        // The step count says how many to read and arrives in the asset. Set
        // against a body that has none of them, it used to be the number of
        // steps the reader would allocate before it gave up, which is as many
        // as the number says.
        const std::string hostile =
            "2\n"          // version
            "71\n"         // key
            "0\n"          // mask
            "/boom\n"      // trigger
            "\n"           // replace text
            "2000000000\n" // step count, and nothing after it
            ;

        std::vector<char> bytes(hostile.begin(), hostile.end());
        bytes.push_back('\0');

        LLMultiGesture read;
        LLDataPackerAsciiBuffer dp(bytes.data(), (S32)bytes.size());
        ensure("a step count with no steps behind it is refused", !read.deserialize(dp));
        ensure("and nothing was built from it", read.mSteps.empty());
    }

    template<> template<>
    void llmultigesture_object::test<4>()
    {
        // The same, with one real step in front of the lie: what was read is
        // kept, the gesture is still refused, and the count does not turn into
        // that many more.
        const std::string hostile =
            "2\n"
            "71\n"
            "0\n"
            "/boom\n"
            "\n"
            "2000000000\n"
            "0\n"              // STEP_ANIMATION
            "express_wave\n"
            "00000000-0000-0000-0000-000000000000\n"
            "0\n"
            ;

        std::vector<char> bytes(hostile.begin(), hostile.end());
        bytes.push_back('\0');

        LLMultiGesture read;
        LLDataPackerAsciiBuffer dp(bytes.data(), (S32)bytes.size());
        ensure("still refused", !read.deserialize(dp));
        ensure("and stopped at the one step the body actually held",
               read.mSteps.size() <= 1u);
    }

    template<> template<>
    void llmultigesture_object::test<5>()
    {
        // Each step reads back what it wrote, and refuses what is missing its
        // last field. Through a whole gesture the difference does not show --
        // the reader gives up on the next step either way -- so the steps are
        // asked directly.
        {
            LLGestureStepAnimation full;
            full.mAnimName = "express_wave";
            full.mAnimAssetID.generate();
            full.mFlags = 1;
            const std::vector<char> bytes = serialize_step(full);

            LLGestureStepAnimation whole;
            ensure("the animation step reads back", step_accepts(whole, bytes));
            ensure_equals("with its name", whole.mAnimName, full.mAnimName);
            ensure_equals("and its flags", whole.mFlags, full.mFlags);

            LLGestureStepAnimation cut;
            ensure("an animation step missing its flags is refused",
                   !step_accepts(cut, without_last_field(bytes)));
        }
        {
            LLGestureStepSound full;
            full.mSoundName = "chime";
            full.mSoundAssetID.generate();
            full.mFlags = 1;
            const std::vector<char> bytes = serialize_step(full);

            LLGestureStepSound whole;
            ensure("the sound step reads back", step_accepts(whole, bytes));
            ensure_equals("with its name", whole.mSoundName, full.mSoundName);

            LLGestureStepSound cut;
            ensure("a sound step missing its flags is refused",
                   !step_accepts(cut, without_last_field(bytes)));
        }
        {
            LLGestureStepChat full;
            full.mChatText = "hello";
            full.mFlags = 1;
            const std::vector<char> bytes = serialize_step(full);

            LLGestureStepChat whole;
            ensure("the chat step reads back", step_accepts(whole, bytes));
            ensure_equals("with its text", whole.mChatText, full.mChatText);
            ensure_equals("and its flags", whole.mFlags, full.mFlags);

            LLGestureStepChat cut;
            ensure("a chat step missing its flags is refused",
                   !step_accepts(cut, without_last_field(bytes)));
        }
        {
            LLGestureStepWait full;
            full.mWaitSeconds = 1.5f;
            full.mFlags = 2;
            const std::vector<char> bytes = serialize_step(full);

            LLGestureStepWait whole;
            ensure("the wait step reads back", step_accepts(whole, bytes));
            ensure_equals("with its flags", whole.mFlags, full.mFlags);

            LLGestureStepWait cut;
            ensure("a wait step missing its flags is refused",
                   !step_accepts(cut, without_last_field(bytes)));
        }
    }

    template<> template<>
    void llmultigesture_object::test<6>()
    {
        // The header, one thing wrong at a time. Each of these is a line in
        // the file, and the file comes from whoever sent the gesture.
        auto read = [](const std::string& text)
        {
            std::vector<char> bytes(text.begin(), text.end());
            bytes.push_back('\0');
            LLMultiGesture gesture;
            LLDataPackerAsciiBuffer dp(bytes.data(), (S32)bytes.size());
            return gesture.deserialize(dp);
        };

        ensure("a version from another format is refused",
               !read("1\n71\n0\n/wave\n\n0\n"));
        ensure("and so is one from no format at all",
               !read("999\n71\n0\n/wave\n\n0\n"));
        ensure("a negative step count is refused",
               !read("2\n71\n0\n/wave\n\n-5\n"));
        ensure("a step type off the end of the list is refused",
               !read("2\n71\n0\n/wave\n\n1\n99\n"));
        ensure("a negative step type is refused",
               !read("2\n71\n0\n/wave\n\n1\n-1\n"));
        ensure("nothing at all is refused", !read(""));
        ensure("and so is a lone newline", !read("\n"));

        // A gesture with no steps in it is a gesture, and loads.
        ensure("no steps is not the same as broken",
               read("2\n71\n0\n/wave\n\n0\n"));
    }

    template<> template<>
    void llmultigesture_object::test<7>()
    {
        // A count smaller than the body leaves the rest unread, which is not
        // an error: the reader takes what it was told to take.
        std::unique_ptr<LLMultiGesture> written = sample_gesture();
        const std::vector<char> bytes = serialize(*written);

        std::vector<char> fewer = bytes;
        const std::string text(bytes.data());

        // the step count is the sixth line
        size_t at = 0;
        for (S32 line = 0; line < 5; ++line)
        {
            at = text.find('\n', at) + 1;
        }
        ensure("the step count line was found", text[at] == '4');
        fewer[at] = '2';

        LLMultiGesture read;
        LLDataPackerAsciiBuffer dp(fewer.data(), (S32)fewer.size());
        ensure("a gesture that claims fewer steps than it holds is taken", read.deserialize(dp));
        ensure_equals("with the number it claimed", read.mSteps.size(), 2u);
    }
}
