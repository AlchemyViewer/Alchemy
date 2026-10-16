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

#include <catch2/catch_test_macros.hpp>

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
        sound->mFlags = 1;
        gesture->mSteps.push_back(sound);

        LLGestureStepChat* chat = new LLGestureStepChat();
        chat->mChatText = "hello";
        chat->mFlags = 1;
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
        REQUIRE(gesture.serialize(dp));
        buffer.resize(dp.getCurrentSize());
        buffer.back() = '\0';
        return buffer;
    }

    // Writes one step out and hands back the bytes, terminator included.
    std::vector<char> serialize_step(const LLGestureStep& step)
    {
        std::vector<char> buffer(step.getMaxSerialSize() + 1, '\0');
        LLDataPackerAsciiBuffer dp(buffer.data(), (S32)buffer.size());
        REQUIRE(step.serialize(dp));
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
        {
            INFO("the step has a last field");
            REQUIRE(last != std::string::npos);
            REQUIRE(last > 0);
        }
        const size_t previous = text.find_last_of('\n', last - 1);
        UNSCOPED_INFO("and one before it");
        REQUIRE(previous != std::string::npos);

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

namespace
{
    TEST_CASE("LLMultiGesture: a gesture it wrote reads back", "[LLMultiGesture]")
    {
        // What it writes it reads back.
        std::unique_ptr<LLMultiGesture> written = sample_gesture();
        std::vector<char> bytes = serialize(*written);

        LLMultiGesture read;
        LLDataPackerAsciiBuffer dp(bytes.data(), (S32)bytes.size());
        REQUIRE(read.deserialize(dp));

        CHECK(read.mKey == written->mKey);
        CHECK(read.mMask == written->mMask);
        CHECK(read.mTrigger == written->mTrigger);
        CHECK(read.mReplaceText == written->mReplaceText);
        REQUIRE(read.mSteps.size() == written->mSteps.size());
        for (size_t i = 0; i < written->mSteps.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(read.mSteps[i]->getType() == written->mSteps[i]->getType());
        }

        const auto& written_anim = *static_cast<LLGestureStepAnimation*>(written->mSteps[0]);
        const auto& read_anim = *static_cast<LLGestureStepAnimation*>(read.mSteps[0]);
        CHECK(read_anim.mAnimName == written_anim.mAnimName);
        CHECK(read_anim.mAnimAssetID == written_anim.mAnimAssetID);
        CHECK(read_anim.mFlags == written_anim.mFlags);

        const auto& written_sound = *static_cast<LLGestureStepSound*>(written->mSteps[1]);
        const auto& read_sound = *static_cast<LLGestureStepSound*>(read.mSteps[1]);
        CHECK(read_sound.mSoundName == written_sound.mSoundName);
        CHECK(read_sound.mSoundAssetID == written_sound.mSoundAssetID);
        CHECK(read_sound.mFlags == written_sound.mFlags);

        const auto& written_chat = *static_cast<LLGestureStepChat*>(written->mSteps[2]);
        const auto& read_chat = *static_cast<LLGestureStepChat*>(read.mSteps[2]);
        CHECK(read_chat.mChatText == written_chat.mChatText);
        CHECK(read_chat.mFlags == written_chat.mFlags);

        const auto& written_wait = *static_cast<LLGestureStepWait*>(written->mSteps[3]);
        const auto& read_wait = *static_cast<LLGestureStepWait*>(read.mSteps[3]);
        CHECK(read_wait.mWaitSeconds == written_wait.mWaitSeconds);
        CHECK(read_wait.mFlags == written_wait.mFlags);
    }

    TEST_CASE("LLMultiGesture: a gesture cut short anywhere is refused", "[LLMultiGesture]")
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
            REQUIRE_FALSE(read.deserialize(dp));
        }
    }

    TEST_CASE("LLMultiGesture: a step count with no steps behind it is refused", "[LLMultiGesture]")
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
        CHECK_FALSE(read.deserialize(dp));
        UNSCOPED_INFO("and nothing was built from it");
        CHECK(read.mSteps.empty());
    }

    TEST_CASE("LLMultiGesture: a step count past one real step is refused", "[LLMultiGesture]")
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
        CHECK_FALSE(read.deserialize(dp));
        UNSCOPED_INFO("and stopped at the one step the body actually held");
        CHECK(read.mSteps.size() <= 1u);
    }

    TEST_CASE("LLMultiGesture: each step reads back and refuses a missing last field", "[LLMultiGesture]")
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
            UNSCOPED_INFO("the animation step reads back");
            CHECK(step_accepts(whole, bytes));
            CHECK(whole.mAnimName == full.mAnimName);
            CHECK(whole.mAnimAssetID == full.mAnimAssetID);
            CHECK(whole.mFlags == full.mFlags);

            LLGestureStepAnimation cut;
            UNSCOPED_INFO("an animation step missing its flags is refused");
            CHECK_FALSE(step_accepts(cut, without_last_field(bytes)));
        }
        {
            LLGestureStepSound full;
            full.mSoundName = "chime";
            full.mSoundAssetID.generate();
            full.mFlags = 1;
            const std::vector<char> bytes = serialize_step(full);

            LLGestureStepSound whole;
            UNSCOPED_INFO("the sound step reads back");
            CHECK(step_accepts(whole, bytes));
            CHECK(whole.mSoundName == full.mSoundName);
            CHECK(whole.mSoundAssetID == full.mSoundAssetID);
            CHECK(whole.mFlags == full.mFlags);

            LLGestureStepSound cut;
            UNSCOPED_INFO("a sound step missing its flags is refused");
            CHECK_FALSE(step_accepts(cut, without_last_field(bytes)));
        }
        {
            LLGestureStepChat full;
            full.mChatText = "hello";
            full.mFlags = 1;
            const std::vector<char> bytes = serialize_step(full);

            LLGestureStepChat whole;
            UNSCOPED_INFO("the chat step reads back");
            CHECK(step_accepts(whole, bytes));
            CHECK(whole.mChatText == full.mChatText);
            CHECK(whole.mFlags == full.mFlags);

            LLGestureStepChat cut;
            UNSCOPED_INFO("a chat step missing its flags is refused");
            CHECK_FALSE(step_accepts(cut, without_last_field(bytes)));
        }
        {
            LLGestureStepWait full;
            full.mWaitSeconds = 1.5f;
            full.mFlags = 2;
            const std::vector<char> bytes = serialize_step(full);

            LLGestureStepWait whole;
            UNSCOPED_INFO("the wait step reads back");
            CHECK(step_accepts(whole, bytes));
            CHECK(whole.mWaitSeconds == full.mWaitSeconds);
            CHECK(whole.mFlags == full.mFlags);

            LLGestureStepWait cut;
            UNSCOPED_INFO("a wait step missing its flags is refused");
            CHECK_FALSE(step_accepts(cut, without_last_field(bytes)));
        }
    }

    TEST_CASE("LLMultiGesture: a bad header is refused and an empty step list loads", "[LLMultiGesture]")
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

        UNSCOPED_INFO("a version from another format is refused");
        CHECK_FALSE(read("1\n71\n0\n/wave\n\n0\n"));
        UNSCOPED_INFO("and so is one from no format at all");
        CHECK_FALSE(read("999\n71\n0\n/wave\n\n0\n"));
        UNSCOPED_INFO("a negative step count is refused");
        CHECK_FALSE(read("2\n71\n0\n/wave\n\n-5\n"));
        UNSCOPED_INFO("a step type off the end of the list is refused");
        CHECK_FALSE(read("2\n71\n0\n/wave\n\n1\n99\n"));
        UNSCOPED_INFO("a negative step type is refused");
        CHECK_FALSE(read("2\n71\n0\n/wave\n\n1\n-1\n"));
        UNSCOPED_INFO("nothing at all is refused");
        CHECK_FALSE(read(""));
        UNSCOPED_INFO("and so is a lone newline");
        CHECK_FALSE(read("\n"));

        // A gesture with no steps in it is a gesture, and loads.
        UNSCOPED_INFO("no steps is not the same as broken");
        CHECK(read("2\n71\n0\n/wave\n\n0\n"));
    }

    TEST_CASE("LLMultiGesture: a step count smaller than the body reads only that many", "[LLMultiGesture]")
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
        UNSCOPED_INFO("the step count line was found");
        CHECK(text[at] == '4');
        fewer[at] = '2';

        LLMultiGesture read;
        LLDataPackerAsciiBuffer dp(fewer.data(), (S32)fewer.size());
        UNSCOPED_INFO("a gesture that claims fewer steps than it holds is taken");
        CHECK(read.deserialize(dp));
        UNSCOPED_INFO("with the number it claimed");
        CHECK(read.mSteps.size() == 2u);
    }
}
