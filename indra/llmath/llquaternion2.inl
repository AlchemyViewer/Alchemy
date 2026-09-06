/**
 * @file llquaternion2.inl
 * @brief LLQuaternion2 inline definitions
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llquaternion2.h"

// Ctor from LLQuaternion
inline LLQuaternion2::LLQuaternion2( const LLQuaternion& quat )
{
    mQ.set(quat.mQ[VX], quat.mQ[VY], quat.mQ[VZ], quat.mQ[VW]);
}

inline const LLQuaternion2& LLQuaternion2::identity()
{
    static const LLQuaternion2 ident(LLVector4a(0.f, 0.f, 0.f, 1.f));
    return ident;
}

//////////////////////////
// Get/Set
//////////////////////////

inline void LLQuaternion2::store( LLQuaternion& dst ) const
{
    _mm_storeu_ps( dst.mQ, (LLQuad)mQ );
}

// Return the internal LLVector4a representation of the quaternion
inline const LLVector4a& LLQuaternion2::getVector4a() const
{
    return mQ;
}

inline LLVector4a& LLQuaternion2::getVector4aRw()
{
    return mQ;
}

/////////////////////////
// Quaternion modification
/////////////////////////

// Set this quaternion to the conjugate of src
inline void LLQuaternion2::setConjugate(const LLQuaternion2& src)
{
    // XOR the sign bit (bit pattern 0x80000000 == -0.f) into x/y/z; leave w.
    const __m128 signMask = _mm_set_ps(0.f, -0.f, -0.f, -0.f);
    mQ = _mm_xor_ps(src.mQ, signMask);
}

// Set this quaternion to the inverse of src
inline void LLQuaternion2::setInverse(const LLQuaternion2& src)
{
    LLVector4a length_squared;
    length_squared.setAllDot4(src.mQ, src.mQ);

    setConjugate(src);
    mQ.div(length_squared);
}

// Set this to a * b, in LLQuaternion's order
inline void LLQuaternion2::setMul(const LLQuaternion2& a, const LLQuaternion2& b)
{
    // LLQuaternion's a * b is the Hamilton product of b with a. Written out,
    // each component is one term from b's scalar part times a, plus a cyclic
    // triple, which is four multiplies of four shuffled operands rather than
    // the sixteen the component form does one at a time.
    //
    //   r.x = b.w*a.x + b.x*a.w + b.y*a.z - b.z*a.y
    //   r.y = b.w*a.y + b.y*a.w + b.z*a.x - b.x*a.z
    //   r.z = b.w*a.z + b.z*a.w + b.x*a.y - b.y*a.x
    //   r.w = b.w*a.w - b.x*a.x - b.y*a.y - b.z*a.z
    //
    // The last column of the second and third terms is the odd one out --
    // subtracted where the others are added -- so it is folded in by flipping
    // the sign bit of w rather than by a separate operation.
    const LLQuad p = (LLQuad)b.mQ;
    const LLQuad q = (LLQuad)a.mQ;

    const LLQuad negate_w = _mm_castsi128_ps(_mm_setr_epi32(0, 0, 0, (int)0x80000000u));

    const LLQuad term_scalar = _mm_mul_ps(_mm_shuffle_ps(p, p, _MM_SHUFFLE(3, 3, 3, 3)), q);
    const LLQuad term_b = _mm_mul_ps(_mm_shuffle_ps(p, p, _MM_SHUFFLE(0, 2, 1, 0)),
                                     _mm_shuffle_ps(q, q, _MM_SHUFFLE(0, 3, 3, 3)));
    const LLQuad term_c = _mm_mul_ps(_mm_shuffle_ps(p, p, _MM_SHUFFLE(1, 0, 2, 1)),
                                     _mm_shuffle_ps(q, q, _MM_SHUFFLE(1, 1, 0, 2)));
    const LLQuad term_d = _mm_mul_ps(_mm_shuffle_ps(p, p, _MM_SHUFFLE(2, 1, 0, 2)),
                                     _mm_shuffle_ps(q, q, _MM_SHUFFLE(2, 0, 2, 1)));

    LLQuad r = _mm_add_ps(term_scalar, _mm_xor_ps(term_b, negate_w));
    r = _mm_add_ps(r, _mm_xor_ps(term_c, negate_w));
    mQ = _mm_sub_ps(r, term_d);
}

// Set this to the normalized lerp from a to b over the shorter way round
inline void LLQuaternion2::setLerp(const LLQuaternion2& a, const LLQuaternion2& b, F32 u)
{
    // A rotation and its negation are the same rotation, so the end that is
    // more than half a turn away is brought round to the near side first --
    // otherwise the interpolation takes the long way and passes through
    // rotations neither end asked for.
    const LLQuad sign_bit = _mm_castsi128_ps(_mm_set1_epi32((int)0x80000000u));

    LLVector4a cos_half_angle;
    cos_half_angle.setAllDot4(a.mQ, b.mQ);
    const LLQuad flip = _mm_and_ps(_mm_cmplt_ps((LLQuad)cos_half_angle, _mm_setzero_ps()), sign_bit);

    LLVector4a near_b;
    near_b = _mm_xor_ps((LLQuad)b.mQ, flip);

    mQ.setLerp(a.mQ, near_b, u);
    mQ.normalize4();
}

// Set this to the interpolation from a to b over the shorter way round, along
// the arc rather than the chord where the two are far enough apart to tell.
inline void LLQuaternion2::setSlerp(const LLQuaternion2& a, const LLQuaternion2& b, F32 u)
{
    // A lerp cuts the chord and an slerp follows the arc, so a lerp arrives
    // early in the middle of the move and late at the ends. How early depends
    // on how far apart the two are: a hundredth of a degree over a twenty
    // degree turn, a fifth of a degree over fifty, and eight degrees over a
    // half turn. Past this the difference is worth the trigonometry; short of
    // it nothing can see it, and most pairs are short of it.
    constexpr F32 SLERP_WORTH_IT = 0.9f;

    const LLQuad sign_bit = _mm_castsi128_ps(_mm_set1_epi32((int)0x80000000u));

    LLVector4a cos_half_angle;
    cos_half_angle.setAllDot4(a.mQ, b.mQ);
    const LLQuad flip = _mm_and_ps(_mm_cmplt_ps((LLQuad)cos_half_angle, _mm_setzero_ps()), sign_bit);

    LLVector4a near_b;
    near_b = _mm_xor_ps((LLQuad)b.mQ, flip);

    const F32 cos_t = fabsf(cos_half_angle.getScalarAt<0>().getF32());
    if (cos_t >= SLERP_WORTH_IT)
    {
        mQ.setLerp(a.mQ, near_b, u);
        mQ.normalize4();
        return;
    }

    const F32 half_angle = acosf(llmin(cos_t, 1.f));
    const F32 sin_half_angle = sinf(half_angle);
    const F32 from = sinf((1.f - u) * half_angle) / sin_half_angle;
    const F32 to = sinf(u * half_angle) / sin_half_angle;

    LLVector4a scaled_a = a.mQ;
    scaled_a.mul(from);
    near_b.mul(to);
    mQ.setAdd(scaled_a, near_b);
    mQ.normalize4();
}

// Renormalizes the quaternion. Assumes it has nonzero length.
inline void LLQuaternion2::normalize()
{
    mQ.normalize4();
}

// Quantize this quaternion to 8 bit precision
inline void LLQuaternion2::quantize8()
{
    mQ.quantize8(_mm_set_ps1(-1.f), _mm_set_ps1(1.f));
    normalize();
}

// Quantize this quaternion to 16 bit precision
inline void LLQuaternion2::quantize16()
{
    mQ.quantize16(_mm_set_ps1(-1.f), _mm_set_ps1(1.f));
    normalize();
}


/////////////////////////
// Quaternion inspection
/////////////////////////

// Rotate a vector by this quaternion
inline void LLQuaternion2::rotate(const LLVector4a& v, LLVector4a& result) const
{
    // q (x) v (x) conj(q), written out in full rather than through the
    // cross-product identity. That identity is a rotation only for a
    // quaternion of unit length, and this is what LLVector3's operator* does
    // for whatever it is handed -- a rotation that has drifted off unit
    // scales the vector, and callers rely on getting the same answer.
    const LLVector4Logical scalar_lane = _mm_castsi128_ps(_mm_setr_epi32(0, 0, 0, -1));

    LLQuaternion2 vector_as_quaternion;
    vector_as_quaternion.mQ.setSelectWithMask(scalar_lane, LLVector4a::getZero(), v);

    // setMul(a, b) composes as LLQuaternion's a * b does, which is the
    // Hamilton product of b with a, so the operands read backwards here.
    LLQuaternion2 rotated;
    rotated.setMul(vector_as_quaternion, *this);

    LLQuaternion2 conjugate;
    conjugate.setConjugate(*this);

    LLQuaternion2 restored;
    restored.setMul(conjugate, rotated);

    // the w handed in comes back, so a caller carrying something there keeps it
    result.setSelectWithMask(scalar_lane, v, restored.mQ);
}

inline LLSimdScalar LLQuaternion2::dot(const LLQuaternion2& rhs) const
{
    return mQ.dot4(rhs.mQ);
}

// Return true if this quaternion is equal to 'rhs'.
// Note! Quaternions exhibit "double-cover", so any rotation has two equally valid
// quaternion representations and they will NOT compare equal.
inline bool LLQuaternion2::equals(const LLQuaternion2 &rhs, F32 tolerance/* = F_APPROXIMATELY_ZERO*/) const
{
    return mQ.equals4(rhs.mQ, tolerance);
}

// Return true if all components are finite and the quaternion is normalized
inline bool LLQuaternion2::isOkRotation() const
{
    return mQ.isFinite4() && mQ.isNormalized4();
}

