/**
 * @file llbvhloader_test.cpp
 * @brief What the BVH reader does with a file it did not write.
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

#include "llbvhloader.h"

#include "lldatapacker.h"
#include "llkeyframemotion.h"
#include "llxmlnode.h"

#include "altestcharacter.h"

#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

namespace
{
    // The lines of a well formed BVH: a root carrying position and rotation,
    // one child carrying rotation, an end site, and four frames. Tests take a
    // copy and put one line wrong.
    enum
    {
        LINE_HIERARCHY = 0,
        LINE_ROOT = 1,
        LINE_ROOT_BRACE = 2,
        LINE_ROOT_OFFSET = 3,
        LINE_ROOT_CHANNELS = 4,
        LINE_JOINT = 5,
        LINE_JOINT_BRACE = 6,
        LINE_JOINT_OFFSET = 7,
        LINE_JOINT_CHANNELS = 8,
        LINE_END_SITE = 9,
        LINE_MOTION = 15,
        LINE_FRAMES = 16,
        LINE_FRAME_TIME = 17,
        LINE_FIRST_FRAME = 18,
        LINE_LAST_FRAME = 21
    };

    std::vector<std::string> sample_lines()
    {
        return {
            "HIERARCHY",
            "ROOT hip",
            "{",
            "  OFFSET 0.00 0.00 0.00",
            "  CHANNELS 6 Xposition Yposition Zposition Xrotation Yrotation Zrotation",
            "  JOINT abdomen",
            "  {",
            "    OFFSET 0.00 3.40 0.00",
            "    CHANNELS 3 Xrotation Yrotation Zrotation",
            "    End Site",
            "    {",
            "      OFFSET 0.00 4.00 0.00",
            "    }",
            "  }",
            "}",
            "MOTION",
            "Frames: 4",
            "Frame Time: 0.033333",
            "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0",
            "1.0 1.0 1.0 10.0 10.0 10.0 5.0 5.0 5.0",
            "2.0 2.0 2.0 20.0 20.0 20.0 10.0 10.0 10.0",
            "3.0 3.0 3.0 30.0 30.0 30.0 15.0 15.0 15.0"
        };
    }

    // Only the count of numbers on the line matters to the reader, so every
    // one of them is the frame index: that moves each joint every frame, which
    // is what stops the optimizer throwing the joint away.
    std::string frame_line(S32 frame, S32 num_floats)
    {
        std::string line;
        for (S32 i = 0; i < num_floats; ++i)
        {
            if (i)
            {
                line += " ";
            }
            line += std::to_string(frame * 10);
        }
        return line;
    }

    // The same file with a first frame that is not all zeroes, so that keys
    // taken relative to the first frame are not the keys themselves.
    std::vector<std::string> moving_lines()
    {
        std::vector<std::string> lines = sample_lines();
        for (S32 frame = 0; frame < 4; ++frame)
        {
            lines[LINE_FIRST_FRAME + frame] = frame_line(frame + 1, 9);
        }
        return lines;
    }

    std::string bvh_text(const std::vector<std::string>& lines)
    {
        std::string text;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            if (i)
            {
                text += "\n";
            }
            text += lines[i];
        }
        return text;
    }

    // The keyframe animation the loader writes, read back with the packer the
    // viewer's own reader uses, so a test can see what came out of a BVH
    // without standing a skeleton up first.
    struct WrittenAnimation
    {
        U16 mVersion = 0;
        U16 mSubVersion = 0;
        S32 mBasePriority = 0;
        F32 mDuration = 0.f;
        std::string mEmoteName;
        F32 mLoopInPoint = 0.f;
        F32 mLoopOutPoint = 0.f;
        S32 mLoop = 0;
        F32 mEaseIn = 0.f;
        F32 mEaseOut = 0.f;
        U32 mHandPose = 0;
        U32 mNumJoints = 0;

        std::vector<std::string> mJointNames;
        std::vector<S32> mJointPriorities;
        std::vector<S32> mNumRotKeys;
        std::vector<S32> mNumPosKeys;
        // four words a key, so that two animations built from the same frames
        // can be compared for whether a table line changed them
        std::vector<std::vector<U16>> mRotKeys;
        std::vector<std::vector<U16>> mPosKeys;

        S32 mNumConstraints = 0;
        // the first constraint, whole
        U8 mChainLength = 0;
        U8 mConstraintType = 0;
        U8 mSourceVolume[16] = {};
        LLVector3 mSourceOffset;
        U8 mTargetVolume[16] = {};
        LLVector3 mTargetOffset;
        LLVector3 mTargetDir;
        F32 mEaseInStart = 0.f;
        F32 mEaseInStop = 0.f;
        F32 mEaseOutStart = 0.f;
        F32 mEaseOutStop = 0.f;
    };

    bool read_written(std::vector<U8>& bytes, WrittenAnimation& out)
    {
        if (bytes.empty())
        {
            return false;
        }

        LLDataPackerBinaryBuffer dp(bytes.data(), (S32)bytes.size());
        bool ok = dp.unpackU16(out.mVersion, "version")
            && dp.unpackU16(out.mSubVersion, "sub_version")
            && dp.unpackS32(out.mBasePriority, "base_priority")
            && dp.unpackF32(out.mDuration, "duration")
            && dp.unpackString(out.mEmoteName, "emote_name")
            && dp.unpackF32(out.mLoopInPoint, "loop_in_point")
            && dp.unpackF32(out.mLoopOutPoint, "loop_out_point")
            && dp.unpackS32(out.mLoop, "loop")
            && dp.unpackF32(out.mEaseIn, "ease_in_duration")
            && dp.unpackF32(out.mEaseOut, "ease_out_duration")
            && dp.unpackU32(out.mHandPose, "hand_pose")
            && dp.unpackU32(out.mNumJoints, "num_joints");

        // The joints are walked rather than skipped, because the constraints
        // are on the other side of them.
        for (U32 j = 0; ok && j < out.mNumJoints && j < 1000; ++j)
        {
            std::string name;
            S32 priority = 0;
            ok = dp.unpackString(name, "joint_name") && dp.unpackS32(priority, "joint_priority");
            out.mJointNames.push_back(name);
            out.mJointPriorities.push_back(priority);

            for (S32 pass = 0; ok && pass < 2; ++pass)
            {
                S32 num_keys = 0;
                ok = dp.unpackS32(num_keys, pass ? "num_pos_keys" : "num_rot_keys")
                    && num_keys >= 0 && num_keys < 100000;
                if (!ok)
                {
                    break;
                }
                (pass ? out.mNumPosKeys : out.mNumRotKeys).push_back(num_keys);

                std::vector<U16> words;
                for (S32 k = 0; ok && k < num_keys; ++k)
                {
                    U16 word = 0;
                    for (S32 part = 0; ok && part < 4; ++part)
                    {
                        ok = dp.unpackU16(word, "key");
                        words.push_back(word);
                    }
                }
                (pass ? out.mPosKeys : out.mRotKeys).push_back(words);
            }
        }

        ok = ok && dp.unpackS32(out.mNumConstraints, "num_constraints");

        if (ok && out.mNumConstraints > 0)
        {
            ok = dp.unpackU8(out.mChainLength, "chain_length")
                && dp.unpackU8(out.mConstraintType, "constraint_type")
                && dp.unpackBinaryDataFixed(out.mSourceVolume, 16, "source_volume")
                && dp.unpackVector3(out.mSourceOffset, "source_offset")
                && dp.unpackBinaryDataFixed(out.mTargetVolume, 16, "target_volume")
                && dp.unpackVector3(out.mTargetOffset, "target_offset")
                && dp.unpackVector3(out.mTargetDir, "target_dir")
                && dp.unpackF32(out.mEaseInStart, "ease_in_start")
                && dp.unpackF32(out.mEaseInStop, "ease_in_stop")
                && dp.unpackF32(out.mEaseOutStart, "ease_out_start")
                && dp.unpackF32(out.mEaseOutStop, "ease_out_stop");
        }

        return ok;
    }


    typedef std::map<std::string, std::string, std::less<>> alias_map_t;

    // The aliases a bone declares, gathered the way LLAvatarAppearance does:
    // the bone's own name is an alias for itself, and the aliases attribute is
    // a list separated by spaces. This is a second reading of the same file
    // rather than a call into that one, because llappearance is built on top
    // of this library and cannot be called from underneath it.
    void collect_bone_aliases(const LLXMLNodePtr& node, alias_map_t& map,
                              std::vector<std::string>& claimed_twice)
    {
        for (LLXMLNodePtr child = node->getFirstChild(); child.notNull(); child = child->getNextSibling())
        {
            std::string name;
            if (child->hasName("bone") && child->getAttributeString("name", name) && !name.empty())
            {
                auto claim = [&](const std::string& alias)
                {
                    alias_map_t::iterator found = map.find(alias);
                    if (found != map.end() && found->second != name)
                    {
                        claimed_twice.push_back(alias);
                    }
                    map[alias] = name;
                };

                claim(name);

                std::string aliases;
                if (child->getAttributeString("aliases", aliases))
                {
                    std::istringstream words(aliases);
                    std::string word;
                    while (words >> word)
                    {
                        claim(word);
                    }
                }
            }

            collect_bone_aliases(child, map, claimed_twice);
        }
    }

    std::string volume_name(const U8 (&field)[16])
    {
        return std::string(reinterpret_cast<const char*>(field));
    }

    // How many bytes of a fixed sixteen the name did not fill. Every one of
    // them is written into the uploaded asset, so every one of them has to be
    // something the loader put there.
    S32 trailing_zeroes(const U8 (&field)[16])
    {
        S32 count = 0;
        for (S32 i = 15; i >= 0 && field[i] == 0; --i)
        {
            count++;
        }
        return count;
    }
}

namespace
{
    struct llbvhloader_data
    {
        ALTestCharacter mCharacter;

        ~llbvhloader_data() { LLKeyframeDataCache::clear(); }

        // The loader has one constructor and it runs the whole pipeline, so it
        // is handed nothing to read and then driven a step at a time, which is
        // what the constructor itself does. Whatever translation table this
        // machine happens to have is thrown away by the reset, so the tests
        // are the only thing that speaks for it.
        std::unique_ptr<LLBVHLoader> makeLoader()
        {
            ELoadStatus status = E_ST_OK;
            S32 error_line = 0;
            std::map<std::string, std::string, std::less<>> aliases;
            auto loader = std::make_unique<LLBVHLoader>("", status, error_line, aliases);
            rearm(*loader);
            return loader;
        }

        // Puts a loader back the way a fresh one arrives, so a sweep of a
        // thousand files does not open a thousand translation tables.
        void rearm(LLBVHLoader& loader)
        {
            loader.reset();
            loader.makeTranslation("hip", "mPelvis");
            loader.makeTranslation("abdomen", "mTorso");
        }

        ELoadStatus load(LLBVHLoader& loader, const std::vector<std::string>& lines)
        {
            return loadText(loader, bvh_text(lines));
        }

        ELoadStatus loadText(LLBVHLoader& loader, const std::string& text)
        {
            char error_text[128] = {};
            S32 error_line = 0;
            return loader.loadBVHFile(text.c_str(), error_text, error_line);
        }

        ELoadStatus readTable(LLBVHLoader& loader, const std::string& text)
        {
            std::istringstream stream(text);
            return loader.loadTranslationTable(stream);
        }

        // The rest of what the constructor does, and then the bytes an upload
        // would carry.
        std::vector<U8> write(LLBVHLoader& loader)
        {
            loader.applyTranslations();
            loader.optimize();
            std::vector<U8> bytes(loader.getOutputSize());
            if (!bytes.empty())
            {
                LLDataPackerBinaryBuffer dp(bytes.data(), (S32)bytes.size());
                loader.serialize(dp);
            }
            return bytes;
        }

        // A translation table, then a BVH, then the animation that comes out.
        WrittenAnimation build(const std::string& table, const std::vector<std::string>& lines)
        {
            CAPTURE(table);
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            REQUIRE(readTable(*loader, table) == E_ST_OK);
            REQUIRE(load(*loader, lines) == E_ST_OK);

            std::vector<U8> bytes = write(*loader);
            WrittenAnimation written;
            REQUIRE(read_written(bytes, written));
            return written;
        }

        // Reads those bytes the way an arriving animation asset is read.
        bool loadAsMotion(LLKeyframeMotion& motion, std::vector<U8>& bytes)
        {
            if (bytes.empty())
            {
                return false;
            }
            LLDataPackerBinaryBuffer dp(bytes.data(), (S32)bytes.size());
            motion.setCharacter(&mCharacter);
            return motion.deserialize(dp, motion.getID());
        }
    };

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: a well formed file is read and written as an animation", "[LLBVHLoader]")
    {
        // A well formed file, and what it says coming out the other side.
        std::unique_ptr<LLBVHLoader> loader = makeLoader();
        CHECK(load(*loader, sample_lines()) == E_ST_OK);

        // Four frames, of which the first is a reference and the last is only
        // interpolated towards, so two of them are played.
        UNSCOPED_INFO("the duration is the frames that play");
        CHECK_THAT(loader->getDuration(), ll_test::WithinBits(2.f * 0.033333f, 16));

        std::vector<U8> bytes = write(*loader);
        WrittenAnimation written;
        REQUIRE(read_written(bytes, written));

        REQUIRE(written.mNumJoints == 2u);
        UNSCOPED_INFO("under the names the table gave them");
        CHECK(written.mJointNames[0] == std::string("mPelvis"));
        CHECK(written.mJointNames[1] == std::string("mTorso"));
        UNSCOPED_INFO("the root's positions are written");
        CHECK(written.mNumPosKeys[0] > 0);
        UNSCOPED_INFO("a three channel joint has no positions to write");
        CHECK(written.mNumPosKeys[1] == 0);
        CHECK(written.mNumRotKeys[0] > 0);
        CHECK(written.mNumRotKeys[1] > 0);
        UNSCOPED_INFO("nothing asked for a constraint");
        CHECK(written.mNumConstraints == 0);

        // An animation shorter than the ease times it was given has them
        // scaled to fit rather than being left longer than itself.
        UNSCOPED_INFO("the eases are scaled down to fit");
        CHECK(written.mEaseIn + written.mEaseOut <= written.mDuration + 1e-5f);
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: LLKeyframeMotion reads what the loader writes", "[LLBVHLoader]")
    {
        // The bytes a BVH turns into are read by the same code that reads an
        // animation somebody sends you, so that is what they are given to.
        std::unique_ptr<LLBVHLoader> loader = makeLoader();
        CHECK(load(*loader, sample_lines()) == E_ST_OK);

        std::vector<U8> bytes = write(*loader);
        LLKeyframeMotion motion(LLUUID::generateNewID());
        UNSCOPED_INFO("what the loader writes is what the reader takes");
        CHECK(loadAsMotion(motion, bytes));
        CHECK(motion.getNumJointMotions() == 2);
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: a malformed hierarchy is refused", "[LLBVHLoader]")
    {
        // The hierarchy, one line wrong at a time.
        auto mutate = [&](const char* what, S32 index, const std::string& text, ELoadStatus expected)
        {
            std::vector<std::string> lines = sample_lines();
            lines[index] = text;
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            INFO(what);
            REQUIRE(load(*loader, lines) == expected);
        };

        mutate("a file that does not say HIERARCHY is refused",
               LINE_HIERARCHY, "GARBAGE", E_ST_NO_HIER);
        mutate("a root with no name is refused",
               LINE_ROOT, "ROOT", E_ST_NO_NAME);
        mutate("a root that is not the pelvis is refused",
               LINE_ROOT, "ROOT elbow", E_ST_BAD_ROOT);
        mutate("a joint that is neither root, joint nor end site is refused",
               LINE_JOINT, "BONE abdomen", E_ST_NO_JOINT);
        mutate("a joint with no name is refused",
               LINE_JOINT, "JOINT", E_ST_NO_NAME);
        mutate("a joint that never opens a brace is refused",
               LINE_ROOT_BRACE, "OFFSET 0 0 0", E_ST_NO_OFFSET);
        mutate("a joint with no offset is refused",
               LINE_ROOT_OFFSET, "CHANNELS 6 Xrotation Yrotation Zrotation", E_ST_NO_OFFSET);
        mutate("a joint with no channels is refused",
               LINE_ROOT_CHANNELS, "OFFSET 0 0 0", E_ST_NO_CHANNELS);

        // Any name the table calls the pelvis will do, which is the whole
        // point of the alias map.
        {
            std::vector<std::string> lines = sample_lines();
            lines[LINE_ROOT] = "ROOT Hips";
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            loader->makeTranslation("Hips", "mPelvis");
            UNSCOPED_INFO("another name for the pelvis is a root");
            CHECK(load(*loader, lines) == E_ST_OK);
        }

        // The three lines of an end site are stepped over without looking at
        // them, so a file that stops partway through one has to stop the read.
        // This pins the answer rather than the stepping: walking a tokenizer
        // past its end lands back on the end in this implementation, so the
        // answer was already this. A debug build is where the difference is,
        // because that is where the iterator asserts.
        for (S32 length = LINE_END_SITE + 1; length <= LINE_END_SITE + 3; ++length)
        {
            CAPTURE(length);
            std::vector<std::string> lines = sample_lines();
            lines.resize(length);
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            UNSCOPED_INFO("a file that ends inside an end site is refused");
            REQUIRE(load(*loader, lines) == E_ST_EOF);
        }
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: a joint takes only three or six channels", "[LLBVHLoader]")
    {
        // Three rotations, or three positions and three rotations, is the
        // whole of what the frame reader takes off a line. Any other count and
        // it would take a number of floats the line never had to carry.
        auto channels = [&](const char* what, const std::string& text, ELoadStatus expected)
        {
            std::vector<std::string> lines = sample_lines();
            lines[LINE_JOINT_CHANNELS] = text;
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            INFO(what);
            REQUIRE(load(*loader, lines) == expected);
        };

        channels("no channels at all is refused",
                 "    CHANNELS 0 Xrotation Yrotation Zrotation", E_ST_NO_CHANNELS);
        channels("one is refused",
                 "    CHANNELS 1 Xrotation Yrotation Zrotation", E_ST_NO_CHANNELS);
        channels("two is refused",
                 "    CHANNELS 2 Xrotation Yrotation Zrotation", E_ST_NO_CHANNELS);
        channels("a negative count is refused",
                 "    CHANNELS -3 Xrotation Yrotation Zrotation", E_ST_NO_CHANNELS);
        channels("and one as large as it will go",
                 "    CHANNELS 2147483647 Xrotation Yrotation Zrotation", E_ST_NO_CHANNELS);
        channels("scale channels are refused rather than read as rotations",
                 "    CHANNELS 9 Xposition Yposition Zposition Xrotation Yrotation Zrotation Xscale Yscale Zscale",
                 E_ST_NO_CHANNELS);

        // No count at all falls back to three for a joint that is not the
        // root, and then there are no rotations on the line to find.
        channels("a channels line with no count still needs its rotations",
                 "    CHANNELS", E_ST_NO_ROTATION);

        // Six channels on a joint that is not the root is allowed, and then
        // every frame line has to be six longer.
        {
            std::vector<std::string> lines = sample_lines();
            lines[LINE_JOINT_CHANNELS] =
                "    CHANNELS 6 Xposition Yposition Zposition Xrotation Yrotation Zrotation";
            for (S32 frame = 0; frame < 4; ++frame)
            {
                lines[LINE_FIRST_FRAME + frame] = frame_line(frame, 12);
            }
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            UNSCOPED_INFO("a six channel child is read");
            CHECK(load(*loader, lines) == E_ST_OK);
        }
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: the rotation order is read from the rotation channel names", "[LLBVHLoader]")
    {
        // The rotation order is read as the letter in front of each of three
        // "rotation" words.
        auto channels = [&](const char* what, const std::string& text, ELoadStatus expected)
        {
            std::vector<std::string> lines = sample_lines();
            lines[LINE_JOINT_CHANNELS] = text;
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            INFO(what);
            REQUIRE(load(*loader, lines) == expected);
        };

        channels("two rotations where three are needed is refused",
                 "    CHANNELS 3 Xrotation Yrotation", E_ST_NO_ROTATION);
        channels("no rotations at all is refused",
                 "    CHANNELS 3 Xposition Yposition Zposition", E_ST_NO_ROTATION);
        channels("an axis that is not one of three letters is refused",
                 "    CHANNELS 3 Wrotation Xrotation Yrotation", E_ST_NO_AXIS);
        channels("a lower case axis is refused",
                 "    CHANNELS 3 xrotation Yrotation Zrotation", E_ST_NO_AXIS);

        // The word at the very head of the line has no letter in front of it
        // to read. This pins the answer, not the reading: the byte before the
        // line is almost never one of three letters, so the answer was already
        // this before the read was stopped from happening. Only a sanitizer
        // sees the difference.
        channels("a rotation with nothing before it is refused",
                 "rotation CHANNELS 3 Xrotation Yrotation Zrotation", E_ST_NO_AXIS);

        channels("the axes may be in any order",
                 "    CHANNELS 3 Zrotation Yrotation Xrotation", E_ST_OK);
        // Not refused: an order naming one axis three times is not an order,
        // and falls back to XYZ rather than being turned away.
        channels("the same axis three times is taken",
                 "    CHANNELS 3 Xrotation Xrotation Xrotation", E_ST_OK);
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: a bad frame count or frame time is refused", "[LLBVHLoader]")
    {
        // The frame count and the frame time, which together are the only
        // thing every key time is written as a fraction of.
        auto mutate = [&](const char* what, S32 index, const std::string& text, ELoadStatus expected)
        {
            std::vector<std::string> lines = sample_lines();
            lines[index] = text;
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            INFO(what);
            REQUIRE(load(*loader, lines) == expected);
        };

        mutate("a frame count that is not a number is refused",
               LINE_FRAMES, "Frames: x", E_ST_NO_FRAMES);
        mutate("a file with no frame count is refused",
               LINE_FRAMES, "Frame Time: 0.03", E_ST_NO_FRAMES);
        mutate("no frames at all is refused",
               LINE_FRAMES, "Frames: 0", E_ST_NO_FRAMES);
        mutate("a negative frame count is refused",
               LINE_FRAMES, "Frames: -5", E_ST_NO_FRAMES);
        mutate("a frame count larger than the file is refused",
               LINE_FRAMES, "Frames: 100", E_ST_EOF);
        mutate("and one as large as it will go",
               LINE_FRAMES, "Frames: 2147483647", E_ST_EOF);

        mutate("a frame time that is not a number is refused",
               LINE_FRAME_TIME, "Frame Time: x", E_ST_NO_FRAME_TIME);
        mutate("a file with no frame time is refused",
               LINE_FRAME_TIME, "Frames: 4", E_ST_NO_FRAME_TIME);
        mutate("a frame time of zero is refused",
               LINE_FRAME_TIME, "Frame Time: 0.0", E_ST_NO_FRAME_TIME);
        mutate("a negative frame time is refused",
               LINE_FRAME_TIME, "Frame Time: -0.033333", E_ST_NO_FRAME_TIME);
        mutate("a frame time of NaN is refused",
               LINE_FRAME_TIME, "Frame Time: nan", E_ST_NO_FRAME_TIME);
        mutate("an infinite frame time is refused",
               LINE_FRAME_TIME, "Frame Time: inf", E_ST_NO_FRAME_TIME);

        // A frame time that is merely enormous is a real number, so the loader
        // takes it and the reader on the other side turns the animation away
        // for being longer than an animation may be.
        {
            std::vector<std::string> lines = sample_lines();
            lines[LINE_FRAME_TIME] = "Frame Time: 1e30";
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            UNSCOPED_INFO("an enormous frame time is read");
            CHECK(load(*loader, lines) == E_ST_OK);

            std::vector<U8> bytes = write(*loader);
            LLKeyframeMotion motion(LLUUID::generateNewID());
            UNSCOPED_INFO("but the animation it makes is too long to play");
            CHECK_FALSE(loadAsMotion(motion, bytes));
        }
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: a malformed frame is refused and extra numbers are dropped", "[LLBVHLoader]")
    {
        // The frames themselves.
        auto mutate = [&](const char* what, S32 index, const std::string& text, ELoadStatus expected)
        {
            std::vector<std::string> lines = sample_lines();
            lines[index] = text;
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            INFO(what);
            REQUIRE(load(*loader, lines) == expected);
        };

        mutate("a frame with fewer numbers than the joints need is refused",
               LINE_FIRST_FRAME, "0.0 0.0 0.0", E_ST_NO_POS);
        mutate("a frame with nothing on it is refused",
               LINE_FIRST_FRAME, "", E_ST_EOF);
        mutate("a frame holding something that is not a number is refused",
               LINE_FIRST_FRAME, "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 x", E_ST_NO_POS);
        mutate("a frame holding a value that is not a number is refused",
               LINE_FIRST_FRAME, "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 nan", E_ST_NO_POS);
        mutate("a frame holding an infinite value is refused",
               LINE_FIRST_FRAME, "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 inf", E_ST_NO_POS);
        mutate("and one large enough to become infinite",
               LINE_FIRST_FRAME, "0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0 1e300", E_ST_NO_POS);

        // Not refused: numbers left over at the end of a frame are dropped,
        // which is how a file with channels nobody asked for still reads.
        mutate("numbers past the end of a frame are ignored",
               LINE_LAST_FRAME, "3.0 3.0 3.0 30.0 30.0 30.0 15.0 15.0 15.0 99.0 99.0",
               E_ST_OK);

        // A file that stops before the frames it promised.
        {
            std::vector<std::string> lines = sample_lines();
            lines.pop_back();
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            UNSCOPED_INFO("a file with fewer frames than it claims is refused");
            CHECK(load(*loader, lines) == E_ST_EOF);
        }
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: every cut of a file is refused except within its last number", "[LLBVHLoader]")
    {
        // Cut anywhere and it is refused rather than half read. Every prefix
        // of a well formed file, so the cut lands in each line and in the
        // middle of each of them in turn.
        //
        // The one place a cut leaves a whole file is inside the last number:
        // "15.0" cut down is still a number, and the file still holds every
        // value it promised. So the sweep is in two halves, and the boundary
        // between them is where the last number starts.
        const std::string whole = bvh_text(sample_lines());
        const size_t last_number = whole.rfind(' ') + 1;
        UNSCOPED_INFO("the file is worth cutting up");
        REQUIRE(whole.size() > 200);
        CHECK(last_number > 1);
        REQUIRE(last_number < whole.size());

        std::unique_ptr<LLBVHLoader> loader = makeLoader();

        for (size_t length = 0; length <= last_number; ++length)
        {
            CAPTURE(length);
            rearm(*loader);
            UNSCOPED_INFO("a BVH cut short is refused");
            REQUIRE(loadText(*loader, whole.substr(0, length)) != E_ST_OK);
        }

        for (size_t length = last_number + 1; length <= whole.size(); ++length)
        {
            CAPTURE(length);
            rearm(*loader);
            UNSCOPED_INFO("a BVH whose last number is merely shorter is whole");
            REQUIRE(loadText(*loader, whole.substr(0, length)) == E_ST_OK);
        }
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: a malformed table is refused and its globals are written", "[LLBVHLoader]")
    {
        // The translation table beside the BVH, which is where everything the
        // BVH itself does not say comes from.
        const std::string header = "Translations 1.0\n";

        auto table = [&](const char* what, const std::string& text, ELoadStatus expected)
        {
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            INFO(what);
            REQUIRE(readTable(*loader, text) == expected);
        };

        table("an empty table is refused", "", E_ST_EOF);
        table("a table from another version is refused", "Translations 2.0\n", E_ST_NO_XLT_HEADER);
        table("a table that is not one is refused", "nonsense\n", E_ST_NO_XLT_HEADER);
        table("a table with nothing in it is a table", header, E_ST_OK);
        table("blank lines and comments are skipped", header + "\n# a comment\n", E_ST_OK);
        table("a section with no name is refused", header + "[\n", E_ST_NO_XLT_NAME);
        table("a priority that is not a number is refused",
              header + "[GLOBALS]\npriority = x\n", E_ST_NO_XLT_PRIORITY);
        table("a hand pose that is not a number is refused",
              header + "[GLOBALS]\nhand = x\n", E_ST_NO_XLT_HAND);
        table("a loop with no value is refused",
              header + "[GLOBALS]\nloop = \n", E_ST_NO_XLT_LOOP);
        table("an ease in with no type is refused",
              header + "[GLOBALS]\neasein = 0.5\n", E_ST_NO_XLT_EASEIN);
        table("an ease out with no type is refused",
              header + "[GLOBALS]\neaseout = 0.5\n", E_ST_NO_XLT_EASEOUT);
        table("a constraint with fields missing is refused",
              header + "[GLOBALS]\nconstraint = 1 2 3\n", E_ST_NO_CONSTRAINT);
        table("a planar constraint with fields missing is refused",
              header + "[GLOBALS]\nplanar_constraint = 1 2 3\n", E_ST_NO_CONSTRAINT);

        // What the globals say is what the written animation carries. The loop
        // points are the ones worth watching: they are fractions of a duration
        // the table is read long before the loader knows.
        {
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            CHECK(readTable(*loader,
                header +
                "[GLOBALS]\n"
                "priority = 4\n"
                "loop = 0.25 0.75\n"
                "easein = 0.1 easeInOut\n"
                "easeout = 0.2 easeInOut\n"
                "hand = 3\n"
                "emote = wave\n") == E_ST_OK);
            CHECK(load(*loader, sample_lines()) == E_ST_OK);

            std::vector<U8> bytes = write(*loader);
            WrittenAnimation written;
            REQUIRE(read_written(bytes, written));

            UNSCOPED_INFO("the priority is the one the table gave");
            CHECK(written.mBasePriority == 4);
            {
                INFO("and every joint is given it");
                REQUIRE(written.mNumJoints == 2u);
                CHECK(written.mJointPriorities[0] == 4);
                CHECK(written.mJointPriorities[1] == 4);
            }
            UNSCOPED_INFO("the hand pose is the one the table gave");
            CHECK(written.mHandPose == 3u);
            UNSCOPED_INFO("so is the emote");
            CHECK(written.mEmoteName == std::string("wave"));
            CHECK(written.mLoop == 1);
            UNSCOPED_INFO("the ease in is not scaled down under a loop");
            CHECK_THAT(written.mEaseIn, ll_test::WithinBits(0.1f, 16));
            UNSCOPED_INFO("nor the ease out");
            CHECK_THAT(written.mEaseOut, ll_test::WithinBits(0.2f, 16));
            CHECK_THAT(written.mLoopInPoint, ll_test::WithinBits(0.25f * written.mDuration, 16));
            CHECK_THAT(written.mLoopOutPoint, ll_test::WithinBits(0.75f * written.mDuration, 16));
            UNSCOPED_INFO("a loop point is somewhere, not nowhere");
            CHECK(written.mLoopOutPoint > 0.f);
        }

        // Without a table saying otherwise the loop runs the whole animation.
        {
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            CHECK(load(*loader, sample_lines()) == E_ST_OK);
            std::vector<U8> bytes = write(*loader);
            WrittenAnimation written;
            REQUIRE(read_written(bytes, written));
            CHECK(written.mLoop == 0);
            UNSCOPED_INFO("the loop still ends at the end");
            CHECK_THAT(written.mLoopOutPoint, ll_test::WithinBits(written.mDuration, 16));
        }
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: a constraint line is written as given and nothing more", "[LLBVHLoader]")
    {
        // A constraint line is read by two formats, the second of which never
        // touches the target direction. Every byte of a constraint is written
        // into the uploaded asset, including the fifteen a name may not fill,
        // so every byte has to be one the loader put there.
        const std::string header = "Translations 1.0\n[GLOBALS]\n";

        {
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            UNSCOPED_INFO("a constraint without a direction is read");
            CHECK(readTable(*loader,
                header + "constraint = 2 0.0 0.2 0.8 1.0 mFootRight 1 2 3 mGroundPlane 4 5 6\n") ==
                E_ST_OK);
            CHECK(load(*loader, sample_lines()) == E_ST_OK);

            std::vector<U8> bytes = write(*loader);
            WrittenAnimation written;
            REQUIRE(read_written(bytes, written));

            CHECK(written.mNumConstraints == 1);
            CHECK((S32)written.mChainLength == 2);
            CHECK((S32)written.mConstraintType == (S32)CONSTRAINT_TYPE_POINT);
            CHECK(volume_name(written.mSourceVolume) == std::string("mFootRight"));
            CHECK(volume_name(written.mTargetVolume) == std::string("mGroundPlane"));
            CHECK(written.mSourceOffset == LLVector3(1.f, 2.f, 3.f));
            CHECK(written.mTargetOffset == LLVector3(4.f, 5.f, 6.f));

            // The line never said a direction, so nothing may be written as
            // one but nothing.
            UNSCOPED_INFO("a direction nobody gave is not a direction");
            CHECK(written.mTargetDir == LLVector3::zero);
            UNSCOPED_INFO("the bytes past the source name are empty");
            CHECK(trailing_zeroes(written.mSourceVolume) == 6);
            UNSCOPED_INFO("and the bytes past the target name");
            CHECK(trailing_zeroes(written.mTargetVolume) == 4);

            CHECK_THAT(written.mEaseInStart, ll_test::WithinBits(0.f, 16));
            CHECK_THAT(written.mEaseInStop, ll_test::WithinBits(0.2f, 16));
            CHECK_THAT(written.mEaseOutStart, ll_test::WithinBits(0.8f, 16));
            CHECK_THAT(written.mEaseOutStop, ll_test::WithinBits(1.f, 16));
        }

        // The longer format does give a direction, and it is normalized.
        {
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            UNSCOPED_INFO("a planar constraint with a direction is read");
            CHECK(readTable(*loader,
                header + "planar_constraint = 2 0.0 0.2 0.8 1.0 mFootRight 0 0 0 mGroundPlane 0 0 0 0 0 4\n") ==
                E_ST_OK);
            CHECK(load(*loader, sample_lines()) == E_ST_OK);

            std::vector<U8> bytes = write(*loader);
            WrittenAnimation written;
            REQUIRE(read_written(bytes, written));
            CHECK((S32)written.mConstraintType == (S32)CONSTRAINT_TYPE_PLANE);
            UNSCOPED_INFO("the direction it gave is normalized");
            // To the rounding of a unit vector: normalizing may take a
            // reciprocal, which the tree lets round a step either way.
            CHECK_THAT(written.mTargetDir.mV[VX], ll_test::WithinRounding(0.f, 1.f));
            CHECK_THAT(written.mTargetDir.mV[VY], ll_test::WithinRounding(0.f, 1.f));
            CHECK_THAT(written.mTargetDir.mV[VZ], ll_test::WithinRounding(1.f, 1.f));
        }

        // A name is read into fifteen of the sixteen bytes and stops there.
        // One that long fills the field; one longer leaves the rest of itself
        // where the next number should be, and the line is refused rather than
        // the name running past the field.
        {
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            UNSCOPED_INFO("a name as long as the field is read");
            CHECK(readTable(*loader,
                header + "constraint = 2 0.0 0.2 0.8 1.0 mAJointWithAVer 0 0 0 mGroundPlane 0 0 0\n") ==
                E_ST_OK);
            CHECK(load(*loader, sample_lines()) == E_ST_OK);

            std::vector<U8> bytes = write(*loader);
            WrittenAnimation written;
            REQUIRE(read_written(bytes, written));
            UNSCOPED_INFO("the name fills the field");
            CHECK(volume_name(written.mSourceVolume) == std::string("mAJointWithAVer"));
            UNSCOPED_INFO("with one byte left to end it");
            CHECK(trailing_zeroes(written.mSourceVolume) == 1);
        }
        {
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            UNSCOPED_INFO("a name longer than the field is refused");
            CHECK(readTable(*loader,
                header + "constraint = 2 0.0 0.2 0.8 1.0 mAJointWithAVeryLongNameIndeed 0 0 0 mGroundPlane 0 0 0\n") ==
                E_ST_NO_CONSTRAINT);
        }
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: a joint section renames or ignores a joint and adjusts its priority", "[LLBVHLoader]")
    {
        // A section named for a joint, and the keys under it that say what
        // becomes of that joint. None of these were read at all before.
        const std::string header = "Translations 1.0\n";

        {
            // The table has the last word over the alias map, which is the
            // whole point of a table: it is read after the aliases now.
            WrittenAnimation written = build(header + "[abdomen]\noutname = mChest\n", sample_lines());
            UNSCOPED_INFO("both joints are still written");
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("the root keeps the name the alias gave it");
            CHECK(written.mJointNames[0] == std::string("mPelvis"));
            UNSCOPED_INFO("and the table renames the other");
            CHECK(written.mJointNames[1] == std::string("mChest"));
        }
        {
            // And the reason it has to be read after them: an alias writes the
            // name and the frame whole, so one applied after a table takes the
            // table's outname away with it.
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            CHECK(readTable(*loader, header + "[abdomen]\noutname = mChest\n") == E_ST_OK);
            loader->makeTranslation("abdomen", "mTorso");
            CHECK(load(*loader, sample_lines()) == E_ST_OK);

            std::vector<U8> bytes = write(*loader);
            WrittenAnimation written;
            REQUIRE(read_written(bytes, written));
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("an alias read after a table undoes what it said");
            CHECK(written.mJointNames[1] == std::string("mTorso"));
        }
        {
            WrittenAnimation written = build(header + "[abdomen]\nignore = true\n", sample_lines());
            UNSCOPED_INFO("an ignored joint is not written");
            REQUIRE(written.mNumJoints == 1u);
            UNSCOPED_INFO("and the one left is the other one");
            CHECK(written.mJointNames[0] == std::string("mPelvis"));
        }
        {
            WrittenAnimation written = build(header + "[abdomen]\nignore = false\n", sample_lines());
            UNSCOPED_INFO("a joint told not to be ignored is written");
            CHECK(written.mNumJoints == 2u);
        }
        {
            WrittenAnimation written = build(
                header + "[GLOBALS]\npriority = 3\n[abdomen]\npriority = 2\n", sample_lines());
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("the base priority is the global one");
            CHECK(written.mBasePriority == 3);
            UNSCOPED_INFO("a joint with no section of its own takes it");
            CHECK(written.mJointPriorities[0] == 3);
            UNSCOPED_INFO("and a joint with one adds its own to it");
            CHECK(written.mJointPriorities[1] == 5);
        }
        {
            WrittenAnimation written = build(
                header + "[GLOBALS]\npriority = 3\n[abdomen]\npriority = -2\n", sample_lines());
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("a modifier may take it down again");
            CHECK(written.mJointPriorities[1] == 1);
        }
        {
            // Both halves come off a line of the file, so their sum is taken
            // wide and brought back into the range a priority is written in.
            WrittenAnimation written = build(
                header + "[GLOBALS]\npriority = 2147483647\n[abdomen]\npriority = 2147483647\n",
                sample_lines());
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("the global priority is written as it was given");
            CHECK(written.mBasePriority == 2147483647);
            UNSCOPED_INFO("but a joint priority is brought into range");
            CHECK(written.mJointPriorities[1] == LL_CHARACTER_MAX_PRIORITY);
        }
        {
            WrittenAnimation written = build(
                header + "[GLOBALS]\npriority = -2147483648\n[abdomen]\npriority = -2147483648\n",
                sample_lines());
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("from the other end too");
            CHECK(written.mJointPriorities[1] == (S32)LLJoint::USE_MOTION_PRIORITY);
        }
        {
            // A key nobody knows is left alone, which is how a table written
            // for a later viewer still reads on this one.
            WrittenAnimation written = build(header + "[abdomen]\nwibble = 3\n", sample_lines());
            UNSCOPED_INFO("an unknown key is ignored");
            CHECK(written.mNumJoints == 2u);
        }
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: a joint key with no joint or a bad value is refused", "[LLBVHLoader]")
    {
        // Every one of those keys with nothing after the equals, or with
        // something the key does not take.
        const std::string joint = "Translations 1.0\n[abdomen]\n";

        auto table = [&](const char* what, const std::string& text, ELoadStatus expected)
        {
            std::unique_ptr<LLBVHLoader> loader = makeLoader();
            INFO(what);
            REQUIRE(readTable(*loader, text) == expected);
        };

        // A key before any section belongs to no joint.
        table("a joint key with no joint is refused",
              "Translations 1.0\nignore = true\n", E_ST_NO_XLT_NAME);
        table("and one in the globals section belongs to no joint either",
              "Translations 1.0\n[GLOBALS]\noutname = mFoo\n", E_ST_NO_XLT_NAME);
        // Not to the last joint named before the globals section, either,
        // which is where it would land if the section did not clear it.
        table("even with a joint named earlier in the file",
              "Translations 1.0\n[abdomen]\n[GLOBALS]\noutname = mFoo\n", E_ST_NO_XLT_NAME);

        table("an ignore with no value is refused", joint + "ignore = \n", E_ST_NO_XLT_IGNORE);

        table("a relative position with two numbers is refused",
              joint + "relativepos = 1 2\n", E_ST_NO_XLT_RELATIVE);
        table("a relative position that is no key it knows is refused",
              joint + "relativepos = lastkey\n", E_ST_NO_XLT_RELATIVE);
        table("a relative position with no value is refused",
              joint + "relativepos = \n", E_ST_NO_XLT_RELATIVE);
        // Subtracted from every position key, which is then quantized by
        // dividing, so it has to be a number.
        table("a relative position that is not a number is refused",
              joint + "relativepos = nan nan nan\n", E_ST_NO_XLT_RELATIVE);
        table("an infinite relative position is refused",
              joint + "relativepos = inf 0 0\n", E_ST_NO_XLT_RELATIVE);

        table("a relative rotation that is no key it knows is refused",
              joint + "relativerot = lastkey\n", E_ST_NO_XLT_RELATIVE);
        table("a relative rotation with no value is refused",
              joint + "relativerot = \n", E_ST_NO_XLT_RELATIVE);

        table("an outname with no name is refused",
              joint + "outname = \n", E_ST_NO_XLT_OUTNAME);

        table("a frame matrix with three numbers is refused",
              joint + "frame = 1 0 0\n", E_ST_NO_XLT_MATRIX);
        table("an offset matrix with six is refused",
              joint + "offset = 1 0 0, 0 1 0\n", E_ST_NO_XLT_MATRIX);
        table("a matrix with no commas in it is refused",
              joint + "frame = 1 0 0 0 1 0 0 0 1\n", E_ST_NO_XLT_MATRIX);

        table("a mergeparent with no name is refused",
              joint + "mergeparent = \n", E_ST_NO_XLT_MERGEPARENT);
        table("a mergechild with no name is refused",
              joint + "mergechild = \n", E_ST_NO_XLT_MERGECHILD);

        table("a joint priority that is not a number is refused",
              joint + "priority = x\n", E_ST_NO_XLT_PRIORITY);
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: relative and matrix keys change only the keyframes they apply to", "[LLBVHLoader]")
    {
        // The keys that change the keyframes rather than the joint. Each is
        // read against the same animation built without it, because what they
        // do is only visible as a difference.
        const std::string header = "Translations 1.0\n";
        const std::vector<std::string> lines = moving_lines();
        const WrittenAnimation plain = build(header, lines);
        REQUIRE(plain.mNumJoints == 2u);
        REQUIRE_FALSE(plain.mRotKeys[1].empty());
        REQUIRE_FALSE(plain.mPosKeys[0].empty());

        {
            // Every position key is moved by this, and five metres is as far
            // as a position key goes, so a hundred puts them all at the end.
            WrittenAnimation written = build(header + "[hip]\nrelativepos = 100 0 0\n", lines);
            REQUIRE(written.mNumJoints == 2u);
            REQUIRE_FALSE(written.mPosKeys[0].empty());
            UNSCOPED_INFO("a relative position moves the position keys");
            CHECK(written.mPosKeys[0] != plain.mPosKeys[0]);
            UNSCOPED_INFO("as far as they go");
            CHECK(written.mPosKeys[0][1] == 0);
            UNSCOPED_INFO("the plain one is nowhere near there");
            CHECK(plain.mPosKeys[0][1] > 30000);
            UNSCOPED_INFO("and the rotations are left alone");
            CHECK(written.mRotKeys[0] == plain.mRotKeys[0]);
        }
        {
            WrittenAnimation written = build(header + "[abdomen]\nrelativerot = firstkey\n", lines);
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("a rotation taken from the first key moves the rotation keys");
            CHECK(written.mRotKeys[1] != plain.mRotKeys[1]);
            UNSCOPED_INFO("and leaves the other joint alone");
            CHECK(written.mRotKeys[0] == plain.mRotKeys[0]);
        }
        {
            WrittenAnimation written = build(
                header + "[abdomen]\nframe = 1 0 0, 0 1 0, 0 0 1\n", lines);
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("a frame matrix moves the rotation keys");
            CHECK(written.mRotKeys[1] != plain.mRotKeys[1]);
            UNSCOPED_INFO("and leaves the other joint alone");
            CHECK(written.mRotKeys[0] == plain.mRotKeys[0]);
        }
        {
            WrittenAnimation written = build(
                header + "[abdomen]\noffset = 0 1 0, 0 0 1, 1 0 0\n", lines);
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("an offset matrix moves the rotation keys");
            CHECK(written.mRotKeys[1] != plain.mRotKeys[1]);
        }
    }

    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: merge keys fold another joint into a rotation", "[LLBVHLoader]")
    {
        // The merge keys, which name another joint whose rotation is folded
        // into this one. Nothing filled these in before, so the arm of the
        // writer that reads them had never run.
        const std::string header = "Translations 1.0\n";
        const std::vector<std::string> lines = moving_lines();
        const WrittenAnimation plain = build(header, lines);
        REQUIRE(plain.mNumJoints == 2u);

        {
            WrittenAnimation written = build(header + "[abdomen]\nmergeparent = hip\n", lines);
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("a merged parent moves the rotation keys");
            CHECK(written.mRotKeys[1] != plain.mRotKeys[1]);
            UNSCOPED_INFO("and leaves the parent alone");
            CHECK(written.mRotKeys[0] == plain.mRotKeys[0]);
        }
        {
            WrittenAnimation written = build(header + "[hip]\nmergechild = abdomen\n", lines);
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("a merged child moves the rotation keys");
            CHECK(written.mRotKeys[0] != plain.mRotKeys[0]);
        }
        {
            // A name that is nobody is nobody, rather than an error: the table
            // is written once and the files come and go.
            WrittenAnimation written = build(header + "[abdomen]\nmergeparent = elbow\n", lines);
            REQUIRE(written.mNumJoints == 2u);
            UNSCOPED_INFO("a merge with a joint the file does not have changes nothing");
            CHECK(written.mRotKeys[1] == plain.mRotKeys[1]);
        }

        // The merged joint is read one frame behind, and the first frame of a
        // single frame animation has no frame behind it. This pins the answer
        // -- a merge with nothing to merge is no merge -- rather than the read
        // that used to go looking for it.
        {
            std::vector<std::string> single = moving_lines();
            single[LINE_FRAMES] = "Frames: 1";
            single.resize(LINE_FIRST_FRAME + 1);

            const WrittenAnimation one_frame = build(header, single);
            UNSCOPED_INFO("a single frame animation is written");
            REQUIRE(one_frame.mNumJoints == 2u);
            CHECK(one_frame.mNumRotKeys[1] == 1);

            WrittenAnimation merged = build(header + "[abdomen]\nmergeparent = hip\n", single);
            REQUIRE(merged.mNumJoints == 2u);
            UNSCOPED_INFO("a merge with no frame behind it is no merge");
            CHECK(merged.mRotKeys[1] == one_frame.mRotKeys[1]);
        }
    }

#ifdef AL_SHIPPED_ANIM_INI
    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: the shipped translation table reads and sets nothing", "[LLBVHLoader]")
    {
        // The table the viewer ships is read by this parser before every BVH
        // upload, and one it refuses stops the upload, so the file itself is
        // read here rather than something that looks like it.
        std::ifstream file(AL_SHIPPED_ANIM_INI);
        UNSCOPED_INFO("the shipped translation table was found");
        CHECK(file.good());

        std::unique_ptr<LLBVHLoader> loader = makeLoader();
        CHECK(loader->loadTranslationTable(file) == E_ST_OK);

        // Nothing in it is a setting, so nothing in it may become one by
        // accident: an example that stopped being a comment would be read.
        std::ifstream again(AL_SHIPPED_ANIM_INI);
        std::string line;
        S32 number = 0;
        while (std::getline(again, line))
        {
            number++;
            CAPTURE(number, line);
            if (number == 1)
            {
                UNSCOPED_INFO("the header is the first line of it");
                REQUIRE(line.compare(0, 16, "Translations 1.0") == 0);
                continue;
            }

            const size_t first = line.find_first_not_of(" \t\r");
            UNSCOPED_INFO("every line after it is blank or a comment");
            REQUIRE((first == std::string::npos || line[first] == '#'));
        }
        UNSCOPED_INFO("the file has something in it");
        CHECK(number > 1);
    }
#endif

#ifdef AL_SHIPPED_AVATAR_SKELETON
    TEST_CASE_METHOD(llbvhloader_data, "LLBVHLoader: a BVH reads against the aliases of the shipped skeleton", "[LLBVHLoader]")
    {
        // Every joint name a .bvh is allowed to use comes from the shipped
        // skeleton, and the root joint check turns away any file whose root is
        // not the pelvis under one of them. Nothing else stands between an
        // edit to that file and every BVH upload failing, so it is read here.
        LLXMLNodePtr root;
        UNSCOPED_INFO("the shipped skeleton was read");
        REQUIRE(LLXMLNode::parseFile(AL_SHIPPED_AVATAR_SKELETON, root, nullptr));
        REQUIRE(root.notNull());

        alias_map_t aliases;
        std::vector<std::string> claimed_twice;
        collect_bone_aliases(root, aliases, claimed_twice);

        UNSCOPED_INFO("the skeleton has its bones");
        REQUIRE(aliases.size() > 100);

        // The name the BVH reader insists the root joint answers to.
        alias_map_t::iterator hip = aliases.find("hip");
        UNSCOPED_INFO("the skeleton still calls the pelvis hip");
        REQUIRE(hip != aliases.end());
        UNSCOPED_INFO("and means the pelvis by it");
        CHECK(hip->second == std::string("mPelvis"));

        // A bone claiming a name another bone already has takes it, and only
        // says so in the log.
        UNSCOPED_INFO("no alias is claimed by two bones");
        CHECK((claimed_twice.empty() ? std::string() : claimed_twice.front()) == std::string());

        // The whole map through the whole reader, which is what the upload
        // does with it.
        std::unique_ptr<LLBVHLoader> loader = makeLoader();
        loader->reset();
        for (const auto& [alias, joint] : aliases)
        {
            loader->makeTranslation(alias, joint);
        }

        UNSCOPED_INFO("a BVH is read against the shipped skeleton");
        CHECK(load(*loader, sample_lines()) == E_ST_OK);

        std::vector<U8> bytes = write(*loader);
        WrittenAnimation written;
        REQUIRE(read_written(bytes, written));
        REQUIRE(written.mNumJoints == 2u);
        UNSCOPED_INFO("the root under the name the skeleton gives it");
        CHECK(written.mJointNames[0] == std::string("mPelvis"));
        CHECK(written.mJointNames[1] == std::string("mTorso"));
    }
#endif
}
