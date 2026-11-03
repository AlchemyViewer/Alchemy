/**
 * @file alobjectmotion_test.cpp
 * @brief The forms of an object update's ObjectData field, by length.
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

#include "../alobjectmotion.h"

#include "llquantize.h"

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

#include <cstring>
#include <vector>

namespace
{
    void put(std::vector<U8>& field, const void* value, size_t size)
    {
        const U8* bytes = static_cast<const U8*>(value);
        field.insert(field.end(), bytes, bytes + size);
    }

    void put_vector3(std::vector<U8>& field, const LLVector3& value)
    {
        put(field, value.mV, sizeof(value.mV));
    }

    const LLVector4 FOOT_PLANE(0.f, 0.f, 1.f, -20.5f);
    const LLVector3 POSITION(12.5f, 200.25f, 31.f);
    const LLVector3 VELOCITY(1.f, -2.f, 0.5f);
    const LLVector3 ACCELERATION(0.f, 0.f, -9.8f);
    const LLVector3 THETA(0.f, 0.f, 0.5f);
    const LLVector3 OMEGA(0.f, 0.25f, 0.f);

    // A full update's field: the foot plane where it is an avatar's, the
    // motion, and then extra bytes of 0xAB.
    std::vector<U8> full_field(bool avatar, size_t extra)
    {
        std::vector<U8> field;
        if (avatar)
        {
            put(field, FOOT_PLANE.mV, sizeof(FOOT_PLANE.mV));
        }
        put_vector3(field, POSITION);
        put_vector3(field, VELOCITY);
        put_vector3(field, ACCELERATION);
        put_vector3(field, THETA);
        put_vector3(field, OMEGA);
        field.insert(field.end(), extra, 0xAB);
        return field;
    }

    void check_full_motion(const ALObjectMotion& motion, bool avatar)
    {
        CHECK(motion.mHasFootPlane == avatar);
        if (avatar)
        {
            CHECK(motion.mFootPlane == FOOT_PLANE);
        }
        CHECK(motion.mPosition == POSITION);
        CHECK(motion.mVelocity == VELOCITY);
        CHECK(motion.mAcceleration == ACCELERATION);
        LLQuaternion rotation;
        rotation.unpackFromVector3(THETA);
        CHECK(motion.mRotation == rotation);
        CHECK(motion.mAngularVelocity == OMEGA);
    }

    TEST_CASE("ALObjectMotion: a full update decodes an object's form and an avatar's", "[ALObjectMotion]")
    {
        const std::vector<U8> object = full_field(false, 0);
        REQUIRE(object.size() == ALObjectMotion::FULL_OBJECT);
        ALObjectMotion object_motion;
        CHECK(object_motion.decodeFull(object.data(), (S32)object.size()));
        check_full_motion(object_motion, false);

        const std::vector<U8> avatar = full_field(true, 0);
        REQUIRE(avatar.size() == ALObjectMotion::FULL_AVATAR);
        ALObjectMotion avatar_motion;
        CHECK(avatar_motion.decodeFull(avatar.data(), (S32)avatar.size()));
        check_full_motion(avatar_motion, true);
    }

    // The 124- and 140-byte forms are the 60- and 76-byte ones followed by
    // 64 bytes this viewer does not read.
    TEST_CASE("ALObjectMotion: a full update's longer forms decode as the shorter ones they begin with", "[ALObjectMotion]")
    {
        const std::vector<U8> object = full_field(false, 64);
        REQUIRE(object.size() == ALObjectMotion::FULL_OBJECT_EXTENDED);
        ALObjectMotion object_motion;
        CHECK(object_motion.decodeFull(object.data(), (S32)object.size()));
        check_full_motion(object_motion, false);

        const std::vector<U8> avatar = full_field(true, 64);
        REQUIRE(avatar.size() == ALObjectMotion::FULL_AVATAR_EXTENDED);
        ALObjectMotion avatar_motion;
        CHECK(avatar_motion.decodeFull(avatar.data(), (S32)avatar.size()));
        check_full_motion(avatar_motion, true);
    }

    // A length that is none of the forms, whether shorter than a form, between
    // two or longer than all of them, decodes nothing: no position at the
    // origin, and no avatar's form for an object.
    TEST_CASE("ALObjectMotion: a full update of any other length decodes nothing", "[ALObjectMotion]")
    {
        const std::vector<U8> field = full_field(true, 255);
        for (S32 length = 0; length <= 255; ++length)
        {
            if (length == ALObjectMotion::FULL_OBJECT || length == ALObjectMotion::FULL_AVATAR
                || length == ALObjectMotion::FULL_OBJECT_EXTENDED || length == ALObjectMotion::FULL_AVATAR_EXTENDED)
            {
                continue;
            }
            CAPTURE(length);
            ALObjectMotion motion;
            if (motion.decodeFull(field.data(), length))
            {
                FAIL_CHECK("a length that is no form decoded");
            }
            if (motion.mHasFootPlane || !motion.mPosition.isExactlyZero())
            {
                FAIL_CHECK("a length that is no form changed the motion");
            }
        }
    }

    // A terse update's field: the foot plane where it is an avatar's, then
    // the position, velocity, acceleration, rotation and angular velocity as
    // the U16s given, then extra bytes of 0xAB.
    std::vector<U8> terse_field(bool avatar, size_t extra)
    {
        std::vector<U8> field;
        if (avatar)
        {
            put(field, FOOT_PLANE.mV, sizeof(FOOT_PLANE.mV));
        }
        const U16 values[] = { 0, 32768, 65535,         // position
                               100, 200, 300,           // velocity
                               65535, 0, 40000,         // acceleration
                               10000, 20000, 30000, 40000, // rotation
                               1, 2, 3 };               // angular velocity
        put(field, values, sizeof(values));
        field.insert(field.end(), extra, 0xAB);
        return field;
    }

    // Each value as U16_to_F32 gives it, to within a fraction of the finest
    // quantum, since the decoder and this file each compile its arithmetic.
    void check_near(const LLVector3& value, const LLVector3& expected)
    {
        for (S32 i = 0; i < 3; ++i)
        {
            CAPTURE(i);
            CHECK_THAT(value.mV[i], ll_test::WithinBits(expected.mV[i], 16));
        }
    }

    void check_terse_motion(const ALObjectMotion& motion, bool avatar)
    {
        const F32 width = 256.f;
        const F32 min_height = -256.f;
        const F32 max_height = 4096.f;
        CHECK(motion.mHasFootPlane == avatar);
        if (avatar)
        {
            CHECK(motion.mFootPlane == FOOT_PLANE);
        }
        check_near(motion.mPosition, LLVector3(U16_to_F32(0, -0.5f * width, 1.5f * width),
                                               U16_to_F32(32768, -0.5f * width, 1.5f * width),
                                               U16_to_F32(65535, min_height, max_height)));
        check_near(motion.mVelocity, LLVector3(U16_to_F32(100, -width, width),
                                               U16_to_F32(200, -width, width),
                                               U16_to_F32(300, -width, width)));
        check_near(motion.mAcceleration, LLVector3(U16_to_F32(65535, -width, width),
                                                   U16_to_F32(0, -width, width),
                                                   U16_to_F32(40000, -width, width)));
        CHECK_THAT(motion.mRotation.mQ[VX], ll_test::WithinBits(U16_to_F32(10000, -1.f, 1.f), 16));
        CHECK_THAT(motion.mRotation.mQ[VY], ll_test::WithinBits(U16_to_F32(20000, -1.f, 1.f), 16));
        CHECK_THAT(motion.mRotation.mQ[VZ], ll_test::WithinBits(U16_to_F32(30000, -1.f, 1.f), 16));
        CHECK_THAT(motion.mRotation.mQ[VW], ll_test::WithinBits(U16_to_F32(40000, -1.f, 1.f), 16));
        check_near(motion.mAngularVelocity, LLVector3(U16_to_F32(1, -width, width),
                                                      U16_to_F32(2, -width, width),
                                                      U16_to_F32(3, -width, width)));
    }

    TEST_CASE("ALObjectMotion: a terse update decodes each of its forms", "[ALObjectMotion]")
    {
        for (bool avatar : { false, true })
        {
            for (size_t extra : { 0, 32 })
            {
                CAPTURE(avatar, extra);
                const std::vector<U8> field = terse_field(avatar, extra);
                ALObjectMotion motion;
                CHECK(motion.decodeTerse(field.data(), (S32)field.size(), 256.f, -256.f, 4096.f));
                check_terse_motion(motion, avatar);
            }
        }
    }

    TEST_CASE("ALObjectMotion: a terse update of any other length decodes nothing", "[ALObjectMotion]")
    {
        const std::vector<U8> field = terse_field(true, 255);
        for (S32 length = 0; length <= 255; ++length)
        {
            if (length == ALObjectMotion::TERSE_OBJECT || length == ALObjectMotion::TERSE_AVATAR
                || length == ALObjectMotion::TERSE_OBJECT_EXTENDED || length == ALObjectMotion::TERSE_AVATAR_EXTENDED)
            {
                continue;
            }
            CAPTURE(length);
            ALObjectMotion motion;
            if (motion.decodeTerse(field.data(), length, 256.f, -256.f, 4096.f))
            {
                FAIL_CHECK("a length that is no form decoded");
            }
            if (motion.mHasFootPlane || !motion.mPosition.isExactlyZero())
            {
                FAIL_CHECK("a length that is no form changed the motion");
            }
        }
    }
}
