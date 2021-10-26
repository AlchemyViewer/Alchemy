/** 
 * @file v2math.h
 * @brief LLVector2 class header file.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#ifndef LL_V2MATH_H
#define LL_V2MATH_H

#include "llmath.h"
#include "v3math.h"

class LLVector4;
class LLMatrix3;
class LLQuaternion;

//  Llvector2 = |x y z w|

static const U32 LENGTHOFVECTOR2 = 2;

class LLVector2
{
	public:
		F32 mV[LENGTHOFVECTOR2];

		static LLVector2 zero;

		inline constexpr LLVector2();							  // Initializes LLVector2 to (0, 0)
		inline constexpr LLVector2(F32 x, F32 y);			      // Initializes LLVector2 to (x. y)
		inline constexpr LLVector2(const F32 *vec);				  // Initializes LLVector2 to (vec[0]. vec[1])
        explicit inline constexpr LLVector2(const LLVector3 &vec); // Initializes LLVector2 to (vec[0]. vec[1])
        explicit inline LLVector2(const LLSD &sd);
		
		// Clears LLVector2 to (0, 0).  DEPRECATED - prefer zeroVec.
		constexpr void	clear();
		constexpr void	setZero();
		constexpr void	clearVec();	// deprecated
		constexpr void	zeroVec();	// deprecated

		constexpr void	set(F32 x, F32 y);	        // Sets LLVector2 to (x, y)
		constexpr void	set(const LLVector2 &vec);	// Sets LLVector2 to vec
		void	set(const F32 *vec);			// Sets LLVector2 to vec

		LLSD	getValue() const;
		void	setValue(const LLSD& sd);

		constexpr void	setVec(F32 x, F32 y);	        // deprecated
		constexpr void	setVec(const LLVector2 &vec);	// deprecated
		void	setVec(const F32 *vec);			// deprecated

		inline bool isFinite() const; // checks to see if all values of LLVector2 are finite

		F32		length() const;				// Returns magnitude of LLVector2
		F32		lengthSquared() const;		// Returns magnitude squared of LLVector2
		F32		normalize();					// Normalizes and returns the magnitude of LLVector2

		F32		magVec() const;				// deprecated
		F32		magVecSquared() const;		// deprecated
		F32		normVec();					// deprecated

		constexpr BOOL	abs();						// sets all values to absolute value of original value (first octant), returns TRUE if changed

		constexpr const LLVector2&	scaleVec(const LLVector2& vec);				// scales per component by vec

		constexpr BOOL isNull();			// Returns TRUE if vector has a _very_small_ length
		constexpr BOOL isExactlyZero() const		{ return !mV[VX] && !mV[VY]; }

		constexpr F32 operator[](int idx) const { return mV[idx]; }
		constexpr F32 &operator[](int idx) { return mV[idx]; }
	
		friend constexpr bool operator<(const LLVector2 &a, const LLVector2 &b);	// For sorting. x is "more significant" than y
		friend constexpr LLVector2 operator+(const LLVector2 &a, const LLVector2 &b);	// Return vector a + b
		friend constexpr LLVector2 operator-(const LLVector2 &a, const LLVector2 &b);	// Return vector a minus b
		friend constexpr F32 operator*(const LLVector2 &a, const LLVector2 &b);		// Return a dot b
		friend constexpr LLVector2 operator%(const LLVector2 &a, const LLVector2 &b);	// Return a cross b
		friend constexpr LLVector2 operator/(const LLVector2 &a, F32 k);				// Return a divided by scaler k
		friend constexpr LLVector2 operator*(const LLVector2 &a, F32 k);				// Return a times scaler k
		friend constexpr LLVector2 operator*(F32 k, const LLVector2 &a);				// Return a times scaler k
		friend constexpr bool operator==(const LLVector2 &a, const LLVector2 &b);		// Return a == b
		friend constexpr bool operator!=(const LLVector2 &a, const LLVector2 &b);		// Return a != b

		friend constexpr const LLVector2& operator+=(LLVector2 &a, const LLVector2 &b);	// Return vector a + b
		friend constexpr const LLVector2& operator-=(LLVector2 &a, const LLVector2 &b);	// Return vector a minus b
		friend constexpr const LLVector2& operator%=(LLVector2 &a, const LLVector2 &b);	// Return a cross b
		friend constexpr const LLVector2& operator*=(LLVector2 &a, F32 k);				// Return a times scaler k
		friend constexpr const LLVector2& operator/=(LLVector2 &a, F32 k);				// Return a divided by scaler k

		friend constexpr LLVector2 operator-(const LLVector2 &a);					// Return vector -a

		friend std::ostream&	 operator<<(std::ostream& s, const LLVector2 &a);		// Stream a
};
static_assert(std::is_trivially_copyable<LLVector2>::value, "LLVector2 must be trivially copyable");
static_assert(std::is_trivially_destructible<LLVector2>::value, "LLVector2 must be trivially destructible");
static_assert(std::is_standard_layout<LLVector2>::value, "LLVector2 must be a standard layout type");

// Non-member functions 

F32	angle_between(const LLVector2 &a, const LLVector2 &b);	// Returns angle (radians) between a and b
BOOL are_parallel(const LLVector2 &a, const LLVector2 &b, F32 epsilon=F_APPROXIMATELY_ZERO);	// Returns TRUE if a and b are very close to parallel
F32	dist_vec(const LLVector2 &a, const LLVector2 &b);		// Returns distance between a and b
F32	dist_vec_squared(const LLVector2 &a, const LLVector2 &b);// Returns distance squared between a and b
F32	dist_vec_squared2D(const LLVector2 &a, const LLVector2 &b);// Returns distance squared between a and b ignoring Z component
LLVector2 lerp(const LLVector2 &a, const LLVector2 &b, F32 u); // Returns a vector that is a linear interpolation between a and b

// Constructors

inline constexpr LLVector2::LLVector2(void)
{
	mV[VX] = 0.f;
	mV[VY] = 0.f;
}

inline constexpr LLVector2::LLVector2(F32 x, F32 y)
{
	mV[VX] = x;
	mV[VY] = y;
}

inline constexpr LLVector2::LLVector2(const F32 *vec)
{
	mV[VX] = vec[VX];
	mV[VY] = vec[VY];
}

inline constexpr LLVector2::LLVector2(const LLVector3 &vec)
{
	mV[VX] = vec.mV[VX];
	mV[VY] = vec.mV[VY];
}

inline LLVector2::LLVector2(const LLSD &sd)
{
    setValue(sd);
}

// Clear and Assignment Functions

inline constexpr void LLVector2::clear(void)
{
	mV[VX] = 0.f;
	mV[VY] = 0.f;
}

inline constexpr void LLVector2::setZero(void)
{
	mV[VX] = 0.f;
	mV[VY] = 0.f;
}

// deprecated
inline constexpr void LLVector2::clearVec(void)
{
	mV[VX] = 0.f;
	mV[VY] = 0.f;
}

// deprecated
inline constexpr void LLVector2::zeroVec(void)
{
	mV[VX] = 0.f;
	mV[VY] = 0.f;
}

inline constexpr void LLVector2::set(F32 x, F32 y)
{
	mV[VX] = x;
	mV[VY] = y;
}

inline constexpr void	LLVector2::set(const LLVector2 &vec)
{
	mV[VX] = vec.mV[VX];
	mV[VY] = vec.mV[VY];
}

inline void	LLVector2::set(const F32 *vec)
{
	mV[VX] = vec[VX];
	mV[VY] = vec[VY];
}


// deprecated
inline constexpr void	LLVector2::setVec(F32 x, F32 y)
{
	mV[VX] = x;
	mV[VY] = y;
}

// deprecated
inline constexpr void	LLVector2::setVec(const LLVector2 &vec)
{
	mV[VX] = vec.mV[VX];
	mV[VY] = vec.mV[VY];
}

// deprecated
inline void	LLVector2::setVec(const F32 *vec)
{
	mV[VX] = vec[VX];
	mV[VY] = vec[VY];
}


// LLVector2 Magnitude and Normalization Functions

inline F32 LLVector2::length(void) const
{
	return (F32) sqrt(mV[0]*mV[0] + mV[1]*mV[1]);
}

inline F32 LLVector2::lengthSquared(void) const
{
	return mV[0]*mV[0] + mV[1]*mV[1];
}

inline F32		LLVector2::normalize(void)
{
	F32 mag = (F32) sqrt(mV[0]*mV[0] + mV[1]*mV[1]);
	F32 oomag;

	if (mag > FP_MAG_THRESHOLD)
	{
		oomag = 1.f/mag;
		mV[0] *= oomag;
		mV[1] *= oomag;
	}
	else
	{
		mV[0] = 0.f;
		mV[1] = 0.f;
		mag = 0;
	}
	return (mag);
}

// checker
inline bool LLVector2::isFinite() const
{
	return (llfinite(mV[VX]) && llfinite(mV[VY]));
}

// deprecated
inline F32		LLVector2::magVec(void) const
{
	return (F32) sqrt(mV[0]*mV[0] + mV[1]*mV[1]);
}

// deprecated
inline F32		LLVector2::magVecSquared(void) const
{
	return mV[0]*mV[0] + mV[1]*mV[1];
}

// deprecated
inline F32		LLVector2::normVec(void)
{
	F32 mag = (F32) sqrt(mV[0]*mV[0] + mV[1]*mV[1]);
	F32 oomag;

	if (mag > FP_MAG_THRESHOLD)
	{
		oomag = 1.f/mag;
		mV[0] *= oomag;
		mV[1] *= oomag;
	}
	else
	{
		mV[0] = 0.f;
		mV[1] = 0.f;
		mag = 0;
	}
	return (mag);
}

inline constexpr const LLVector2& LLVector2::scaleVec(const LLVector2& vec)
{
	mV[VX] *= vec.mV[VX];
	mV[VY] *= vec.mV[VY];

	return *this;
}

// Sets all values to absolute value of their original values
// Returns TRUE if data changed
inline constexpr BOOL LLVector2::abs()
{
	BOOL ret = FALSE;

	if (mV[0] < 0.f) { mV[0] = -mV[0]; ret = TRUE; }
	if (mV[1] < 0.f) { mV[1] = -mV[1]; ret = TRUE; }

	return ret;
}

inline constexpr BOOL LLVector2::isNull()
{
	if ( F_APPROXIMATELY_ZERO > mV[VX]*mV[VX] + mV[VY]*mV[VY] )
	{
		return TRUE;
	}
	return FALSE;
}


// LLVector2 Operators

// For sorting. By convention, x is "more significant" than y.
inline constexpr bool operator<(const LLVector2 &a, const LLVector2 &b)
{
	if( a.mV[VX] == b.mV[VX] )
	{
		return a.mV[VY] < b.mV[VY];
	}
	else
	{
		return a.mV[VX] < b.mV[VX];
	}
}


inline constexpr LLVector2 operator+(const LLVector2 &a, const LLVector2 &b)
{
	LLVector2 c(a);
	return c += b;
}

inline constexpr LLVector2 operator-(const LLVector2 &a, const LLVector2 &b)
{
	LLVector2 c(a);
	return c -= b;
}

inline constexpr F32  operator*(const LLVector2 &a, const LLVector2 &b)
{
	return (a.mV[0]*b.mV[0] + a.mV[1]*b.mV[1]);
}

inline constexpr LLVector2 operator%(const LLVector2 &a, const LLVector2 &b)
{
	return LLVector2(a.mV[0]*b.mV[1] - b.mV[0]*a.mV[1], a.mV[1]*b.mV[0] - b.mV[1]*a.mV[0]);
}

inline constexpr LLVector2 operator/(const LLVector2 &a, F32 k)
{
	F32 t = 1.f / k;
	return LLVector2( a.mV[0] * t, a.mV[1] * t );
}

inline constexpr LLVector2 operator*(const LLVector2 &a, F32 k)
{
	return LLVector2( a.mV[0] * k, a.mV[1] * k );
}

inline constexpr LLVector2 operator*(F32 k, const LLVector2 &a)
{
	return LLVector2( a.mV[0] * k, a.mV[1] * k );
}

inline constexpr bool operator==(const LLVector2 &a, const LLVector2 &b)
{
	return (  (a.mV[0] == b.mV[0])
			&&(a.mV[1] == b.mV[1]));
}

inline constexpr bool operator!=(const LLVector2 &a, const LLVector2 &b)
{
	return (  (a.mV[0] != b.mV[0])
			||(a.mV[1] != b.mV[1]));
}

inline constexpr const LLVector2& operator+=(LLVector2 &a, const LLVector2 &b)
{
	a.mV[0] += b.mV[0];
	a.mV[1] += b.mV[1];
	return a;
}

inline constexpr const LLVector2& operator-=(LLVector2 &a, const LLVector2 &b)
{
	a.mV[0] -= b.mV[0];
	a.mV[1] -= b.mV[1];
	return a;
}

inline constexpr const LLVector2& operator%=(LLVector2 &a, const LLVector2 &b)
{
	LLVector2 ret(a.mV[0]*b.mV[1] - b.mV[0]*a.mV[1], a.mV[1]*b.mV[0] - b.mV[1]*a.mV[0]);
	a = ret;
	return a;
}

inline constexpr const LLVector2& operator*=(LLVector2 &a, F32 k)
{
	a.mV[0] *= k;
	a.mV[1] *= k;
	return a;
}

inline constexpr const LLVector2& operator/=(LLVector2 &a, F32 k)
{
	F32 t = 1.f / k;
	a.mV[0] *= t;
	a.mV[1] *= t;
	return a;
}

inline constexpr LLVector2 operator-(const LLVector2 &a)
{
	return LLVector2( -a.mV[0], -a.mV[1] );
}

inline void update_min_max(LLVector2& min, LLVector2& max, const LLVector2& pos)
{
	for (U32 i = 0; i < 2; i++)
	{
		if (min.mV[i] > pos.mV[i])
		{
			min.mV[i] = pos.mV[i];
		}
		if (max.mV[i] < pos.mV[i])
		{
			max.mV[i] = pos.mV[i];
		}
	}
}

inline std::ostream& operator<<(std::ostream& s, const LLVector2 &a) 
{
	s << "{ " << a.mV[VX] << ", " << a.mV[VY] << " }";
	return s;
}

#endif
